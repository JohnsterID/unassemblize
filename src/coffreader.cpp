#include "coffreader.h"
#include "asmmatcher.h"
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace {
constexpr uint16_t IMAGE_FILE_MACHINE_I386 = 0x014c;
constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;

constexpr uint32_t IMAGE_SCN_CNT_CODE = 0x00000020;
constexpr uint32_t IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040;
constexpr uint32_t IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080;
constexpr uint32_t IMAGE_SCN_MEM_EXECUTE = 0x20000000;
constexpr uint32_t IMAGE_SCN_MEM_READ = 0x40000000;
constexpr uint32_t IMAGE_SCN_MEM_WRITE = 0x80000000;
} // namespace

std::string COFFReader::read_string(const std::vector<char>& string_table, uint32_t offset) {
    if (offset >= string_table.size()) {
        return "";
    }
    const char* str = string_table.data() + offset;
    return std::string(str, strnlen(str, string_table.size() - offset));
}

std::vector<Symbol> COFFReader::parse_symbols(std::ifstream& file, const COFF::Header& header) {
    std::vector<Symbol> symbols;
    file.seekg(header.PointerToSymbolTable);

    // First read string table size
    file.seekg(header.PointerToSymbolTable + header.NumberOfSymbols * sizeof(COFF::SymbolRecord));
    uint32_t string_table_size;
    file.read(reinterpret_cast<char*>(&string_table_size), sizeof(uint32_t));

    // Read string table
    std::vector<char> string_table;
    if (string_table_size > sizeof(uint32_t)) {
        string_table.resize(string_table_size - sizeof(uint32_t));
        file.read(string_table.data(), string_table_size - sizeof(uint32_t));
    }

    // Read symbols
    file.seekg(header.PointerToSymbolTable);
    for (uint32_t i = 0; i < header.NumberOfSymbols; ++i) {
        COFF::SymbolRecord symbol;
        file.read(reinterpret_cast<char*>(&symbol), sizeof(COFF::SymbolRecord));

        std::string name;
        if (symbol.Name.ShortName[0] == 0) {
            // Long name from string table
            uint32_t offset = symbol.Name.LongName.Offset;
            name = read_string(string_table, offset);
        } else {
            // Short name directly from symbol record
            char short_name[9] = { 0 }; // One extra for null termination
            std::memcpy(short_name, symbol.Name.ShortName, 8);
            name = short_name;
        }

        if (!name.empty()) {
            Symbol sym;
            sym.name = name;
            sym.value = symbol.Value;
            sym.section = symbol.SectionNumber;
            sym.type = symbol.Type;
            sym.storage_class = symbol.StorageClass;
            symbols.push_back(sym);
        }

        // Skip auxiliary symbol records
        i += symbol.NumberOfAuxSymbols;
        file.seekg(symbol.NumberOfAuxSymbols * sizeof(COFF::SymbolRecord), std::ios::cur);
    }

    return symbols;
}

std::vector<Section> COFFReader::parse_sections(std::ifstream& file, const COFF::Header& header) {
    std::vector<Section> sections;
    
    // Skip optional header if present
    file.seekg(sizeof(COFF::Header) + header.SizeOfOptionalHeader);

    for (uint16_t i = 0; i < header.NumberOfSections; ++i) {
        COFF::SectionHeader section_header;
        file.read(reinterpret_cast<char*>(&section_header), sizeof(COFF::SectionHeader));

        Section section;
        section.name = std::string(section_header.Name, strnlen(section_header.Name, sizeof(section_header.Name)));
        section.virtual_size = section_header.VirtualSize;
        section.virtual_address = section_header.VirtualAddress;
        section.size = section_header.SizeOfRawData;
        section.characteristics = section_header.Characteristics;

        // Read section data
        if (section_header.PointerToRawData > 0 && section_header.SizeOfRawData > 0) {
            auto current_pos = file.tellg();
            file.seekg(section_header.PointerToRawData);
            section.content.resize(section_header.SizeOfRawData);
            file.read(reinterpret_cast<char*>(section.content.data()), section_header.SizeOfRawData);
            file.seekg(current_pos);
        }

        sections.push_back(std::move(section));
    }

    return sections;
}

ExecutableFile COFFReader::parse(const std::string& filepath) {
    ExecutableFile result;
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }

    // Read COFF header
    COFF::Header header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    // Verify machine type
    if (header.Machine != IMAGE_FILE_MACHINE_I386 && header.Machine != IMAGE_FILE_MACHINE_AMD64) {
        throw std::runtime_error("Unsupported machine type");
    }

    // Parse symbols and sections
    result.symbols = parse_symbols(file, header);
    result.sections = parse_sections(file, header);

    // Set file format
    result.format = ExecutableFormat::COFF;

    return result;
}