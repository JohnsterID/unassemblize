/**
 * @file
 *
 * @brief Function types
 *
 * @copyright Unassemblize is free software: you can redistribute it and/or
 *            modify it under the terms of the GNU General Public License
 *            as published by the Free Software Foundation, either version
 *            3 of the License, or (at your option) any later version.
 *            A full copy of the GNU General Public License can be found in
 *            LICENSE
 */
#pragma once

#include "commontypes.h"
#include <array>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace unassemblize
{
constexpr std::string_view s_prefix_sub = "sub_";
constexpr std::string_view s_prefix_off = "off_";
constexpr std::string_view s_prefix_unk = "unk_";
constexpr std::string_view s_prefix_loc = "loc_";
constexpr std::array<std::string_view, 4> s_prefix_array = {s_prefix_sub, s_prefix_off, s_prefix_unk, s_prefix_loc};

// #TODO: implement default value where exe object decides internally what to do.
enum class AsmFormat
{
    IGAS,
    AGAS,
    MASM,

    DEFAULT,
};

AsmFormat to_asm_format(const char *str);

/*
 * Intermediate instruction data between Zydis disassemble and final text generation.
 */
struct AsmInstruction
{
    AsmInstruction()
    {
        address = 0;
        isJump = false;
        isInvalid = false;
        jumpLen = 0;
    }

    Address64T address; // Position of the instruction within the executable.
    bool isJump : 1; // Instruction is a jump.
    bool isInvalid : 1; // Instruction was not read or formatted correctly.
    union
    {
        int16_t jumpLen; // Jump length in bytes.
    };
    std::string text; // Instruction mnemonics and operands with address symbol substitution.
};

struct AsmLabel
{
    std::string label;
};

struct AsmNull
{
};

using AsmInstructionVariant = std::variant<AsmLabel, AsmInstruction, AsmNull>;
using AsmInstructionVariants = std::vector<AsmInstructionVariant>;

} // namespace unassemblize
