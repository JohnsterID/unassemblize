#pragma once

#include "commontypes.h"
#include "executable.h"
#include <cstdint>
#include <string>
#include <vector>

namespace COFF {
#pragma pack(push, 1)
struct Header {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct SectionHeader {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLineNumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLineNumbers;
    uint32_t Characteristics;
};

struct SymbolRecord {
    union {
        char ShortName[8];
        struct {
            uint32_t Zeros;
            uint32_t Offset;
        } LongName;
    } Name;
    uint32_t Value;
    int16_t SectionNumber;
    uint16_t Type;
    uint8_t StorageClass;
    uint8_t NumberOfAuxSymbols;
};

struct StringTable {
    uint32_t Size;  // Including the size field itself
    // Followed by null-terminated strings
};
#pragma pack(pop)
} // namespace COFF

class COFFReader {
public:
    COFFReader() = default;
    ~COFFReader() = default;

    ExecutableFile parse(const std::string& filepath);

private:
    std::string read_string(const std::vector<char>& string_table, uint32_t offset);
    std::vector<Symbol> parse_symbols(std::ifstream& file, const COFF::Header& header);
    std::vector<Section> parse_sections(std::ifstream& file, const COFF::Header& header);
};