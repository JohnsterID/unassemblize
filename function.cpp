/**
 * @file
 *
 * @brief Class encapsulating a single function disassembly.
 *
 * @copyright Unassemblize is free software: you can redistribute it and/or
 *            modify it under the terms of the GNU General Public License
 *            as published by the Free Software Foundation, either version
 *            3 of the License, or (at your option) any later version.
 *            A full copy of the GNU General Public License can be found in
 *            LICENSE
 */
#include "function.h"
#include <Zycore/Format.h>
#include <Zydis/Zydis.h>
#include <fmt/core.h>
#include <inttypes.h>
#include <iomanip>
#include <sstream>
#include <string.h>

namespace unassemblize
{
namespace
{
uint32_t get_le32(const uint8_t *data)
{
    return (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
}

enum class JumpType
{
    None,
    Short,
    Long,
};

JumpType IsJump(const ZydisDecodedInstruction *instruction, const ZydisDecodedOperand *operand)
{
    // Check if the instruction is a jump (JMP, conditional jump, etc.)
    switch (instruction->mnemonic) {
        case ZYDIS_MNEMONIC_JB:
        case ZYDIS_MNEMONIC_JBE:
        case ZYDIS_MNEMONIC_JCXZ:
        case ZYDIS_MNEMONIC_JECXZ:
        case ZYDIS_MNEMONIC_JKNZD:
        case ZYDIS_MNEMONIC_JKZD:
        case ZYDIS_MNEMONIC_JL:
        case ZYDIS_MNEMONIC_JLE:
        case ZYDIS_MNEMONIC_JMP:
        case ZYDIS_MNEMONIC_JNB:
        case ZYDIS_MNEMONIC_JNBE:
        case ZYDIS_MNEMONIC_JNL:
        case ZYDIS_MNEMONIC_JNLE:
        case ZYDIS_MNEMONIC_JNO:
        case ZYDIS_MNEMONIC_JNP:
        case ZYDIS_MNEMONIC_JNS:
        case ZYDIS_MNEMONIC_JNZ:
        case ZYDIS_MNEMONIC_JO:
        case ZYDIS_MNEMONIC_JP:
        case ZYDIS_MNEMONIC_JRCXZ:
        case ZYDIS_MNEMONIC_JS:
        case ZYDIS_MNEMONIC_JZ:
            break;
        default:
            return JumpType::None;
    }

    // Check if the first operand is a relative immediate.
    if (operand->type == ZYDIS_OPERAND_TYPE_IMMEDIATE && operand->imm.is_relative) {
        // Short jumps have an 8-bit immediate value (1 byte)
        if (operand->size == 8) {
            return JumpType::Short;
        } else {
            return JumpType::Long;
        }
    }

    return JumpType::None;
}

bool HasBaseRegister(const ZydisDecodedOperand *operand)
{
    return operand->mem.base > ZYDIS_REGISTER_NONE && operand->mem.base <= ZYDIS_REGISTER_MAX_VALUE;
}

bool HasIndexRegister(const ZydisDecodedOperand *operand)
{
    return operand->mem.index > ZYDIS_REGISTER_NONE && operand->mem.index <= ZYDIS_REGISTER_MAX_VALUE;
}

bool HasBaseOrIndexRegister(const ZydisDecodedOperand *operand)
{
    return HasBaseRegister(operand) || HasIndexRegister(operand);
}

bool HasIrrelevantSegment(const ZydisDecodedOperand *operand)
{
    switch (operand->mem.segment) {
        case ZYDIS_REGISTER_ES:
        case ZYDIS_REGISTER_SS:
        case ZYDIS_REGISTER_FS:
        case ZYDIS_REGISTER_GS:
            return true;
        case ZYDIS_REGISTER_CS:
        case ZYDIS_REGISTER_DS:
        default:
            return false;
    }
}

bool GetStackWidth(ZydisStackWidth &stack_width, ZydisMachineMode machine_mode)
{
    switch (machine_mode) {
        case ZYDIS_MACHINE_MODE_LONG_64:
            stack_width = ZYDIS_STACK_WIDTH_64;
            return true;
        case ZYDIS_MACHINE_MODE_LONG_COMPAT_32:
        case ZYDIS_MACHINE_MODE_LEGACY_32:
            stack_width = ZYDIS_STACK_WIDTH_32;
            return true;
        case ZYDIS_MACHINE_MODE_LONG_COMPAT_16:
        case ZYDIS_MACHINE_MODE_LEGACY_16:
        case ZYDIS_MACHINE_MODE_REAL_16:
            stack_width = ZYDIS_STACK_WIDTH_16;
            return true;
        default:
            return false;
    }
}

} // namespace

void append_as_text(std::string &str, const InstructionDataVector &instructions)
{
    for (const InstructionData &instruction : instructions) {
        if (!instruction.label.empty()) {
            str += fmt::format("{:s}:\n", instruction.label);
        }
        if (instruction.isInvalid) {
            // Add unrecognized instruction as comment.
            str += fmt::format("; Unrecognized opcode at runtime-address:0x{:08X} bytes:{:s}",
                instruction.address,
                instruction.instruction);
        } else {
            str += fmt::format("    {:s}", instruction.instruction);
        }

        if (instruction.isJump) {
            // Add jump distance as inline comment.
            str += fmt::format(" ; {:+d} bytes", instruction.jumpLen);
        }

        str += "\n";
    }
}

FunctionSetup::FunctionSetup(const Executable &executable, AsmFormat format) : executable(executable), format(format)
{
    ZyanStatus status = initialize();
    assert(status == ZYAN_STATUS_SUCCESS);
}

ZyanStatus FunctionSetup::initialize()
{
    // Derive the stack width from the address width.
    constexpr ZydisMachineMode machine_mode = ZYDIS_MACHINE_MODE_LEGACY_32;

    if (!GetStackWidth(stack_width, machine_mode)) {
        return false;
    }

    if (ZYAN_FAILED(ZydisDecoderInit(&decoder, machine_mode, stack_width))) {
        return false;
    }

    switch (format) {
        case AsmFormat::MASM:
            style = ZYDIS_FORMATTER_STYLE_INTEL_MASM;
            break;
        case AsmFormat::AGAS:
            style = ZYDIS_FORMATTER_STYLE_ATT;
            break;
        case AsmFormat::IGAS:
        case AsmFormat::DEFAULT:
        default:
            style = ZYDIS_FORMATTER_STYLE_INTEL;
            break;
    }

    ZYAN_CHECK(ZydisFormatterInit(&formatter, style));
    ZYAN_CHECK(ZydisFormatterSetProperty(&formatter, ZYDIS_FORMATTER_PROP_FORCE_SIZE, ZYAN_TRUE));

    default_print_address_absolute = static_cast<ZydisFormatterFunc>(&Function::UnasmFormatterPrintAddressAbsolute);
    default_print_address_relative = static_cast<ZydisFormatterFunc>(&Function::UnasmFormatterPrintAddressRelative);
    default_print_displacement = static_cast<ZydisFormatterFunc>(&Function::UnasmFormatterPrintDISP);
    default_print_immediate = static_cast<ZydisFormatterFunc>(&Function::UnasmFormatterPrintIMM);
    default_format_operand_ptr = static_cast<ZydisFormatterFunc>(&Function::UnasmFormatterFormatOperandPTR);
    default_format_operand_mem = static_cast<ZydisFormatterFunc>(&Function::UnasmFormatterFormatOperandMEM);
    default_print_register = static_cast<ZydisFormatterRegisterFunc>(&Function::UnasmFormatterPrintRegister);

    // clang-format off
    ZYAN_CHECK(ZydisFormatterSetHook(&formatter, ZYDIS_FORMATTER_FUNC_PRINT_ADDRESS_ABS, (const void **)&default_print_address_absolute));
    ZYAN_CHECK(ZydisFormatterSetHook(&formatter, ZYDIS_FORMATTER_FUNC_PRINT_ADDRESS_REL, (const void **)&default_print_address_relative));
    ZYAN_CHECK(ZydisFormatterSetHook(&formatter, ZYDIS_FORMATTER_FUNC_PRINT_DISP, (const void **)&default_print_displacement));
    ZYAN_CHECK(ZydisFormatterSetHook(&formatter, ZYDIS_FORMATTER_FUNC_PRINT_IMM, (const void **)&default_print_immediate));
    ZYAN_CHECK(ZydisFormatterSetHook(&formatter, ZYDIS_FORMATTER_FUNC_FORMAT_OPERAND_PTR, (const void **)&default_format_operand_ptr));
    ZYAN_CHECK(ZydisFormatterSetHook(&formatter, ZYDIS_FORMATTER_FUNC_FORMAT_OPERAND_MEM, (const void **)&default_format_operand_mem));
    ZYAN_CHECK(ZydisFormatterSetHook(&formatter, ZYDIS_FORMATTER_FUNC_PRINT_REGISTER, (const void **)&default_print_register));
    // clang-format on

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus Function::UnasmFormatterPrintAddressAbsolute(
    const ZydisFormatter *formatter, ZydisFormatterBuffer *buffer, ZydisFormatterContext *context)
{
    Function *func = static_cast<Function *>(context->user_data);
    ZyanU64 address;
    ZYAN_CHECK(ZydisCalcAbsoluteAddress(context->instruction, context->operand, context->runtime_address, &address));

    if (context->operand->imm.is_relative) {
        address += func->get_executable().image_base();
    }

    // Does not look for symbol when address is in irrelevant segment, such as fs:[0]
    if (!HasIrrelevantSegment(context->operand)) {
        const ExeSymbol &symbol = func->get_symbol_from_image_base(address);

        if (!symbol.name.empty()) {
            ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
            ZyanString *string;
            ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
            const JumpType jump_type = IsJump(context->instruction, context->operand);
            if (jump_type == JumpType::Short) {
                ZYAN_CHECK(ZyanStringAppendFormat(string, "short "));
            }
            ZYAN_CHECK(ZyanStringAppendFormat(string, "%s", symbol.name.c_str()));
            return ZYAN_STATUS_SUCCESS;
        }

        if (address >= func->get_executable().text_section_begin_from_image_base()
            && address < func->get_executable().text_section_end_from_image_base()) {
            ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
            ZyanString *string;
            ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));

            ZYAN_CHECK(ZyanStringAppendFormat(string, "sub_%" PRIx64, address));
            return ZYAN_STATUS_SUCCESS;
        }

        if (address >= func->get_executable().all_sections_begin_from_image_base()
            && address < func->get_executable().all_sections_end_from_image_base()) {
            ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
            ZyanString *string;
            ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
            ZYAN_CHECK(ZyanStringAppendFormat(string, "off_%" PRIx64, address));
            return ZYAN_STATUS_SUCCESS;
        }
    }

    return func->get_default_print_address_absolute()(formatter, buffer, context);
}

ZyanStatus Function::UnasmFormatterPrintAddressRelative(
    const ZydisFormatter *formatter, ZydisFormatterBuffer *buffer, ZydisFormatterContext *context)
{
    Function *func = static_cast<Function *>(context->user_data);
    ZyanU64 address;
    ZYAN_CHECK(ZydisCalcAbsoluteAddress(context->instruction, context->operand, context->runtime_address, &address));

    if (context->operand->imm.is_relative) {
        address += func->get_executable().image_base();
    }

    const ExeSymbol &symbol = func->get_symbol_from_image_base(address);

    if (!symbol.name.empty()) {
        ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
        ZyanString *string;
        ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
        ZYAN_CHECK(ZyanStringAppendFormat(string, "%s", symbol.name.c_str()));
        return ZYAN_STATUS_SUCCESS;
    }

    if (address >= func->get_executable().text_section_begin_from_image_base()
        && address < func->get_executable().text_section_end_from_image_base()) {
        ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
        ZyanString *string;
        ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
        ZYAN_CHECK(ZyanStringAppendFormat(string, "sub_%" PRIx64, address));
        return ZYAN_STATUS_SUCCESS;
    }

    if (address >= func->get_executable().all_sections_begin_from_image_base()
        && address < func->get_executable().all_sections_end_from_image_base()) {
        ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
        ZyanString *string;
        ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
        ZYAN_CHECK(ZyanStringAppendFormat(string, "off_%" PRIx64, address));
        return ZYAN_STATUS_SUCCESS;
    }

    return func->get_default_print_address_relative()(formatter, buffer, context);
}

ZyanStatus Function::UnasmFormatterPrintDISP(
    const ZydisFormatter *formatter, ZydisFormatterBuffer *buffer, ZydisFormatterContext *context)
{
    Function *func = static_cast<Function *>(context->user_data);

    if (context->operand->mem.disp.value < 0) {
        return func->get_default_print_displacement()(formatter, buffer, context);
    }

    ZyanU64 value = static_cast<ZyanU64>(context->operand->mem.disp.value);

    if (context->operand->imm.is_relative) {
        value += func->get_executable().image_base();
    }

    // Does not look for symbol when address is in irrelevant segment, such as fs:[0]
    if (!HasIrrelevantSegment(context->operand)) {
        // Does not look for symbol when there is an operand with a register plus offset, such as [eax+0x400e00]
        if (!HasBaseOrIndexRegister(context->operand)) {
            const ExeSymbol &symbol = func->get_symbol_from_image_base(value);

            if (!symbol.name.empty()) {
                ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
                ZyanString *string;
                ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
                ZYAN_CHECK(ZyanStringAppendFormat(string, "+%s", symbol.name.c_str()));
                return ZYAN_STATUS_SUCCESS;
            }
        }

        if (value >= func->get_executable().text_section_begin_from_image_base()
            && value < func->get_executable().text_section_end_from_image_base()) {
            ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
            ZyanString *string;
            ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
            ZYAN_CHECK(ZyanStringAppendFormat(string, "+sub_%" PRIx64, value));
            return ZYAN_STATUS_SUCCESS;
        }

        if (value >= func->get_executable().all_sections_begin_from_image_base()
            && value < (func->get_executable().all_sections_end_from_image_base())) {
            ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
            ZyanString *string;
            ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
            ZYAN_CHECK(ZyanStringAppendFormat(string, "+off_%" PRIx64, value));
            return ZYAN_STATUS_SUCCESS;
        }
    }

    return func->get_default_print_displacement()(formatter, buffer, context);
}

ZyanStatus Function::UnasmFormatterPrintIMM(
    const ZydisFormatter *formatter, ZydisFormatterBuffer *buffer, ZydisFormatterContext *context)
{
    Function *func = static_cast<Function *>(context->user_data);
    ZyanU64 value = context->operand->imm.value.u;

    if (context->operand->imm.is_relative) {
        value += func->get_executable().image_base();
    }

    // Does not look for symbol when address is in irrelevant segment, such as fs:[0]
    if (!HasIrrelevantSegment(context->operand)) {
        // Does not look for symbol when there is an operand with a register plus offset, such as [eax+0x400e00]
        if (!HasBaseOrIndexRegister(context->operand)) {
            // Note: Immediate values, such as "push 0x400400" could be considered a symbol.
            // Right now there is no clever way to avoid this.
            const ExeSymbol &symbol = func->get_symbol_from_image_base(value);

            if (!symbol.name.empty()) {
                ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
                ZyanString *string;
                ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
                ZYAN_CHECK(ZyanStringAppendFormat(string, "offset %s", symbol.name.c_str()));
                return ZYAN_STATUS_SUCCESS;
            }
        }

        if (value >= func->get_executable().text_section_begin_from_image_base()
            && value < func->get_executable().text_section_end_from_image_base()) {
            ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
            ZyanString *string;
            ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
            ZYAN_CHECK(ZyanStringAppendFormat(string, "offset sub_%" PRIx64, value));
            return ZYAN_STATUS_SUCCESS;
        }

        if (value >= func->get_executable().all_sections_begin_from_image_base()
            && value < (func->get_executable().all_sections_end_from_image_base())) {
            ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
            ZyanString *string;
            ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
            ZYAN_CHECK(ZyanStringAppendFormat(string, "offset off_%" PRIx64, value));
            return ZYAN_STATUS_SUCCESS;
        }
    }

    return func->get_default_print_immediate()(formatter, buffer, context);
}

ZyanStatus Function::UnasmFormatterFormatOperandPTR(
    const ZydisFormatter *formatter, ZydisFormatterBuffer *buffer, ZydisFormatterContext *context)
{
    Function *func = static_cast<Function *>(context->user_data);
    ZyanU64 offset = context->operand->ptr.offset;

    if (context->operand->imm.is_relative) {
        offset += func->get_executable().image_base();
    }

    const ExeSymbol &symbol = func->get_symbol_from_image_base(offset);

    if (!symbol.name.empty()) {
        ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
        ZyanString *string;
        ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
        ZYAN_CHECK(ZyanStringAppendFormat(string, "%s", symbol.name.c_str()));
        return ZYAN_STATUS_SUCCESS;
    }

    if (offset >= func->get_executable().text_section_begin_from_image_base()
        && offset < func->get_executable().text_section_end_from_image_base()) {
        ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
        ZyanString *string;
        ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
        ZYAN_CHECK(ZyanStringAppendFormat(string, "sub_%" PRIx64, offset));
        return ZYAN_STATUS_SUCCESS;
    }

    if (offset >= func->get_executable().all_sections_begin_from_image_base()
        && offset < func->get_executable().all_sections_end_from_image_base()) {
        ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
        ZyanString *string;
        ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
        ZYAN_CHECK(ZyanStringAppendFormat(string, "unk_%" PRIx64, offset));
        return ZYAN_STATUS_SUCCESS;
    }

    return func->get_default_format_operand_ptr()(formatter, buffer, context);
}

ZyanStatus Function::UnasmFormatterFormatOperandMEM(
    const ZydisFormatter *formatter, ZydisFormatterBuffer *buffer, ZydisFormatterContext *context)
{
    Function *func = static_cast<Function *>(context->user_data);

    if (context->operand->mem.disp.value < 0) {
        return func->get_default_format_operand_mem()(formatter, buffer, context);
    }

    ZyanU64 value = static_cast<ZyanU64>(context->operand->mem.disp.value);

    if (context->operand->imm.is_relative) {
        value += func->get_executable().image_base();
    }

    // Does not look for symbol when address is in irrelevant segment, such as fs:[0]
    if (!HasIrrelevantSegment(context->operand)) {
        // Does not look for symbol when there is an operand with a register plus offset, such as [eax+0x400e00]
        if (!HasBaseOrIndexRegister(context->operand)) {
            const ExeSymbol &symbol = func->get_symbol_from_image_base(value);

            if (!symbol.name.empty()) {
                if ((context->operand->mem.type == ZYDIS_MEMOP_TYPE_MEM)
                    || (context->operand->mem.type == ZYDIS_MEMOP_TYPE_VSIB)) {
                    ZYAN_CHECK(formatter->func_print_typecast(formatter, buffer, context));
                }
                ZYAN_CHECK(formatter->func_print_segment(formatter, buffer, context));
                ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
                ZyanString *string;
                ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
                ZYAN_CHECK(ZyanStringAppendFormat(string, "[%s]", symbol.name.c_str()));
                return ZYAN_STATUS_SUCCESS;
            }
        }

        if (value >= func->get_executable().text_section_begin_from_image_base()
            && value < func->get_executable().text_section_end_from_image_base()) {
            if ((context->operand->mem.type == ZYDIS_MEMOP_TYPE_MEM)
                || (context->operand->mem.type == ZYDIS_MEMOP_TYPE_VSIB)) {
                ZYAN_CHECK(formatter->func_print_typecast(formatter, buffer, context));
            }
            ZYAN_CHECK(formatter->func_print_segment(formatter, buffer, context));
            ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
            ZyanString *string;
            ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
            ZYAN_CHECK(ZyanStringAppendFormat(string, "[sub_%" PRIx64 "]", value));
            return ZYAN_STATUS_SUCCESS;
        }

        if (value >= func->get_executable().all_sections_begin_from_image_base()
            && value < func->get_executable().all_sections_end_from_image_base()) {
            if ((context->operand->mem.type == ZYDIS_MEMOP_TYPE_MEM)
                || (context->operand->mem.type == ZYDIS_MEMOP_TYPE_VSIB)) {
                ZYAN_CHECK(formatter->func_print_typecast(formatter, buffer, context));
            }
            ZYAN_CHECK(formatter->func_print_segment(formatter, buffer, context));
            ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, ZYDIS_TOKEN_SYMBOL));
            ZyanString *string;
            ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
            ZYAN_CHECK(ZyanStringAppendFormat(string, "[unk_%" PRIx64 "]", value));
            return ZYAN_STATUS_SUCCESS;
        }
    }

    return func->get_default_format_operand_mem()(formatter, buffer, context);
}

ZyanStatus Function::UnasmFormatterPrintRegister(
    const ZydisFormatter *formatter, ZydisFormatterBuffer *buffer, ZydisFormatterContext *context, ZydisRegister reg)
{
    // Copied from internal FormatterBase.h
#define ZYDIS_BUFFER_APPEND_TOKEN(buffer, type) \
    if ((buffer)->is_token_list) { \
        ZYAN_CHECK(ZydisFormatterBufferAppend(buffer, type)); \
    }

    if (reg >= ZYDIS_REGISTER_ST0 && reg <= ZYDIS_REGISTER_ST7) {
        ZYDIS_BUFFER_APPEND_TOKEN(buffer, ZYDIS_TOKEN_REGISTER);
        ZyanString *string;
        ZYAN_CHECK(ZydisFormatterBufferGetString(buffer, &string));
        ZYAN_CHECK(ZyanStringAppendFormat(string, "st(%d)", reg - 69));
        return ZYAN_STATUS_SUCCESS;
    }

    Function *func = static_cast<Function *>(context->user_data);
    return func->get_default_print_register()(formatter, buffer, context, reg);
}

// Copy of the disassemble function without any formatting.
ZyanStatus Function::UnasmDisassembleNoFormat(const ZydisDecoder &decoder, ZyanU64 runtime_address, const void *buffer,
    ZyanUSize length, ZydisDisassembledInstruction &instruction)
{
    assert(buffer != nullptr);

    memset(&instruction, 0, sizeof(instruction));
    instruction.runtime_address = runtime_address;

    ZydisDecoderContext ctx;
    ZYAN_CHECK(ZydisDecoderDecodeInstruction(&decoder, &ctx, buffer, length, &instruction.info));
    ZYAN_CHECK(
        ZydisDecoderDecodeOperands(&decoder, &ctx, &instruction.info, instruction.operands, instruction.info.operand_count));

    return ZYAN_STATUS_SUCCESS;
}

ZyanStatus Function::UnasmDisassembleCustom(const ZydisFormatter &formatter, const ZydisDecoder &decoder,
    ZyanU64 runtime_address, const void *buffer, ZyanUSize length, ZydisDisassembledInstruction &instruction,
    std::string &instruction_buffer, void *user_data)
{
    assert(buffer != nullptr);

    memset(&instruction, 0, sizeof(instruction));
    instruction.runtime_address = runtime_address;

    ZydisDecoderContext ctx;
    ZYAN_CHECK(ZydisDecoderDecodeInstruction(&decoder, &ctx, buffer, length, &instruction.info));
    ZYAN_CHECK(
        ZydisDecoderDecodeOperands(&decoder, &ctx, &instruction.info, instruction.operands, instruction.info.operand_count));

    ZYAN_CHECK(ZydisFormatterFormatInstruction(&formatter,
        &instruction.info,
        instruction.operands,
        instruction.info.operand_count_visible,
        &instruction_buffer[0],
        instruction_buffer.size(),
        runtime_address,
        user_data));

    return ZYAN_STATUS_SUCCESS;
}

void Function::disassemble(const FunctionSetup *setup, Address64T begin_address, Address64T end_address)
{
    const ExeSectionInfo *section_info = setup->executable.find_section(begin_address);

    if (section_info == nullptr) {
        return;
    }

    Address64T runtime_address = begin_address;
    const Address64T address_offset = section_info->address;
    Address64T section_offset = begin_address - address_offset;
    const Address64T section_offset_end = end_address - address_offset;

    if (section_offset_end - section_offset > section_info->size) {
        return;
    }

    assert(setup != nullptr);

    m_instructions.clear();
    m_setup = setup;
    m_beginAddress = begin_address;
    m_endAddress = end_address;

    const Address64T image_base = setup->executable.image_base();
    const uint8_t *section_data = section_info->data;
    const Address64T section_size = section_info->size;

    ZydisDisassembledInstruction instruction;
    std::string instruction_buffer;
    instruction_buffer.resize(1024);

    size_t instruction_count = 0;

    // Loop through function once to identify all jumps to local labels and create them.
    while (section_offset < section_offset_end) {
        const Address64T instruction_address = runtime_address;
        const Address64T instruction_section_offset = section_offset;

        const ZyanStatus status = UnasmDisassembleNoFormat(setup->decoder,
            instruction_address,
            section_data + instruction_section_offset,
            section_size - instruction_section_offset,
            instruction);

        runtime_address += instruction.info.length;
        section_offset += instruction.info.length;
        ++instruction_count;

        if (!ZYAN_SUCCESS(status)) {
            continue;
        }

        // Add pseudo symbols for jump or call target addresses.
        if (instruction.info.raw.imm[0].is_relative) {
            Address64T absolute_address;
            ZydisCalcAbsoluteAddress(&instruction.info, instruction.operands, instruction_address, &absolute_address);

            if (absolute_address >= begin_address && absolute_address < end_address) {
                add_pseudo_symbol(absolute_address);
            }
        }
    }

    m_instructions.resize(instruction_count);
    section_offset = begin_address - address_offset;
    runtime_address = begin_address;

    size_t instruction_index = 0;
    while (section_offset < section_offset_end) {
        InstructionData &instruction_data = m_instructions[instruction_index];
        const Address64T instruction_address = runtime_address;
        const Address64T instruction_section_offset = section_offset;
        instruction_data.address = runtime_address;

        const ZyanStatus status = UnasmDisassembleCustom(setup->formatter,
            setup->decoder,
            instruction_address,
            section_data + instruction_section_offset,
            section_size - instruction_section_offset,
            instruction,
            instruction_buffer,
            this);

        runtime_address += instruction.info.length;
        section_offset += instruction.info.length;
        ++instruction_index;

        if (!ZYAN_SUCCESS(status)) {
            instruction_data.isInvalid = true;
            for (ZyanU8 i = 0; i < instruction.info.length; ++i) {
                instruction_data.instruction += fmt::format("{:02X}", section_data[instruction_section_offset + i]);
            }
            continue;
        }

        {
            const ExeSymbol &symbol = get_symbol(instruction_address);
            if (!symbol.name.empty()) {
                instruction_data.label = symbol.name;
            }
        }

        instruction_data.instruction = instruction_buffer.c_str();

        const JumpType jump_type = IsJump(&instruction.info, &instruction.operands[0]);

        if (jump_type == JumpType::Short) {
            const int64_t offset = instruction.operands[0].imm.value.s;
            assert(std::abs(offset) < (1 << (sizeof(InstructionData::jumpLen) * 8)) / 2);
            instruction_data.isJump = true;
            instruction_data.jumpLen = offset;
        } else if (jump_type == JumpType::Long) {
            // Is only stable when jumping within a single function.
            ZyanU64 jump_address;
            if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(
                    &instruction.info, &instruction.operands[0], instruction_address, &jump_address))) {
                if (jump_address >= get_begin_address() && jump_address < get_end_address()) {
                    const int64_t offset = int64_t(jump_address) - int64_t(instruction_address);
#if 0 // Cannot assert this when disassembling a range beyond a single function.
                     assert(std::abs(offset) < (1 << (sizeof(InstructionData::jumpLen) * 8)) / 2);
#endif
                    instruction_data.isJump = true;
                    instruction_data.jumpLen = offset;
                }
            }
        }
    }

    assert(instruction_index == instruction_count);

    m_pseudoSymbols.swap(ExeSymbols());
    m_pseudoSymbolAddressToIndexMap.swap(Address64ToIndexMap());
}

void Function::add_pseudo_symbol(Address64T address)
{
    {
        const ExeSymbol &symbol = m_setup->executable.get_symbol(address);
        if (symbol.address != 0) {
            return;
        }
    }

    Address64ToIndexMap::iterator it = m_pseudoSymbolAddressToIndexMap.find(address);

    if (it == m_pseudoSymbolAddressToIndexMap.end()) {
        ExeSymbol symbol;
        symbol.name = fmt::format("loc_{:X}", address);
        symbol.address = address;
        symbol.size = 0;

        const uint32_t index = static_cast<uint32_t>(m_pseudoSymbols.size());
        m_pseudoSymbols.push_back(symbol);
        m_pseudoSymbolAddressToIndexMap[symbol.address] = index;
    }
}

const InstructionDataVector &Function::get_instructions() const
{
    return m_instructions;
}

const Executable &Function::get_executable() const
{
    return m_setup->executable;
}

ZydisFormatterFunc Function::get_default_print_address_absolute() const
{
    return m_setup->default_print_address_absolute;
}

ZydisFormatterFunc Function::get_default_print_address_relative() const
{
    return m_setup->default_print_address_relative;
}

ZydisFormatterFunc Function::get_default_print_displacement() const
{
    return m_setup->default_print_displacement;
}

ZydisFormatterFunc Function::get_default_print_immediate() const
{
    return m_setup->default_print_immediate;
}

ZydisFormatterFunc Function::get_default_format_operand_mem() const
{
    return m_setup->default_format_operand_mem;
}

ZydisFormatterFunc Function::get_default_format_operand_ptr() const
{
    return m_setup->default_format_operand_ptr;
}

ZydisFormatterRegisterFunc Function::get_default_print_register() const
{
    return m_setup->default_print_register;
}

const ExeSymbol &Function::get_symbol(Address64T address) const
{
    Address64ToIndexMap::const_iterator it = m_pseudoSymbolAddressToIndexMap.find(address);

    if (it != m_pseudoSymbolAddressToIndexMap.end()) {
        return m_pseudoSymbols[it->second];
    }

    return m_setup->executable.get_symbol(address);
}

const ExeSymbol &Function::get_symbol_from_image_base(Address64T address) const
{
#if 0 // Cannot put assert here as long as there are symbol lookup guesses left.
    if (!(addr >= m_executable.all_sections_begin_from_image_base()
            && addr < m_executable.all_sections_end_from_image_base())) {
        __debugbreak();
    }
#endif

    Address64ToIndexMap::const_iterator it =
        m_pseudoSymbolAddressToIndexMap.find(address - m_setup->executable.image_base());

    if (it != m_pseudoSymbolAddressToIndexMap.end()) {
        return m_pseudoSymbols[it->second];
    }

    return m_setup->executable.get_symbol_from_image_base(address);
}

const ExeSymbol &Function::get_nearest_symbol(Address64T address) const
{
    Address64ToIndexMap::const_iterator it = m_pseudoSymbolAddressToIndexMap.lower_bound(address);

    if (it != m_pseudoSymbolAddressToIndexMap.end()) {
        const ExeSymbol &symbol = m_pseudoSymbols[it->second];
        if (symbol.address == address) {
            return symbol;
        } else {
            const ExeSymbol &prevSymbol = m_pseudoSymbols[std::prev(it)->second];
            return prevSymbol;
        }
    }

    return m_setup->executable.get_nearest_symbol(address);
}

Address64T Function::get_begin_address() const
{
    return m_beginAddress;
}

Address64T Function::get_end_address() const
{
    return m_endAddress;
}

} // namespace unassemblize
