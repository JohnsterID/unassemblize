/**
 * @file
 *
 * @brief main function and option handling.
 *
 * @copyright Assemblize is free software: you can redistribute it and/or
 *            modify it under the terms of the GNU General Public License
 *            as published by the Free Software Foundation, either version
 *            3 of the License, or (at your option) any later version.
 *            A full copy of the GNU General Public License can be found in
 *            LICENSE
 */
#include "function.h"
#include "gitinfo.h"
#include <LIEF/LIEF.hpp>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <strings.h>

void print_help()
{
    char revision[12] = {0};
    const char *version = GitTag[0] == 'v' ? GitTag : GitShortSHA1;

    if (GitTag[0] != 'v') {
        snprintf(revision, sizeof(revision), "r%d ", GitRevision);
    }

    printf(
        "\nunassemblize %s%s%s\n"
        "    x86 Unassembly tool\n\n"
        "Usage:\n"
        "  unassemblize [OPTIONS] [INPUT]\n"
        "Options:\n"
        "  -o --output     Filename for single file output. Default is program.S\n"
        "  -f --format     Assembly output format.\n"
        "  -c --config     Configuration file describing how to dissassemble the input\n"
        "                  file and containing extra symbol info. Default: config.json\n"
        "  -s --start      Starting address of a single function to dissassemble in\n"
        "                  hexidecimal notation.\n"
        "  -e --end        Ending address of a single function to dissassemble in\n"
        "                  hexidecimal notation.\n"
        "  -v --verbose    Verbose output on current state of the program.\n"
        "  --section       Section to target for dissassembly, defaults to '.text'.\n"
        "  --listsections  Prints a list of sections in the exe then exits.\n"
        "  -d --dumpsyms   Dumps symbols stored in the executable to the config file.\n"
        "                  then exits.\n"
        "  -h --help       Displays this help.\n\n",
        revision,
        GitUncommittedChanges ? "~" : "",
        version);
}

void print_sections(unassemblize::Executable &exe)
{
    const unassemblize::Executable::SectionMap &map = exe.get_section_map();
    for (auto it = map.begin(); it != map.end(); ++it) {
        printf(
            "Name: %s, Address: 0x%" PRIx64 " Size: %" PRIu64 "\n", it->first.c_str(), it->second.address, it->second.size);
    }
}

void dump_function_to_file(
    const char *file_name, unassemblize::Executable &exe, const char *section_name, uint64_t start, uint64_t end)
{
    assert(file_name != nullptr);
    FILE *fp = fopen(file_name, "w+");
    if (fp != nullptr) {
        fprintf(fp, ".intel_syntax noprefix\n\n");
        exe.dissassemble_function(fp, section_name, start, end);
        fclose(fp);
    }
}

void remove_characters(std::string &s, const std::string &chars)
{
    s.erase(
        std::remove_if(s.begin(), s.end(), [&chars](const char &c) { return chars.find(c) != std::string::npos; }), s.end());
}

int main(int argc, char **argv)
{
    if (argc <= 1) {
        print_help();
        return -1;
    }

    const char *section_name = ".text";
    const char *output = "program.S";
    const char *config_file = "config.json";
    const char *format_string = nullptr;
    uint64_t start_addr = 0;
    uint64_t end_addr = 0;
    bool print_secs = false;
    bool dump_syms = false;
    bool verbose = false;

    while (true) {
        static struct option long_options[] = {
            {"output", required_argument, nullptr, 'o'},
            {"format", required_argument, nullptr, 'f'},
            {"start", required_argument, nullptr, 's'},
            {"end", required_argument, nullptr, 'e'},
            {"config", required_argument, nullptr, 'c'},
            {"section", required_argument, nullptr, 1},
            {"listsections", no_argument, nullptr, 2},
            {"dumpsyms", no_argument, nullptr, 'd'},
            {"verbose", no_argument, nullptr, 'v'},
            {"help", no_argument, nullptr, 'h'},
            {nullptr, no_argument, nullptr, 0},
        };

        int option_index = 0;

        int c = getopt_long(argc, argv, "+dhv?o:f:s:e:c:", long_options, &option_index);

        if (c == -1) {
            break;
        }

        switch (c) {
            case 1:
                section_name = optarg;
                break;
            case 2:
                print_secs = true;
                break;
            case 'd':
                dump_syms = true;
                break;
            case 'o':
                output = optarg;
                break;
            case 'f':
                format_string = optarg;
                break;
            case 's':
                start_addr = strtoull(optarg, nullptr, 16);
                break;
            case 'e':
                end_addr = strtoull(optarg, nullptr, 16);
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'v':
                verbose = true;
                break;
            case '?':
                printf("\nOption %d not recognised.\n", optopt);
                print_help();
                return 0;
            case ':':
                printf("\nAn option is missing arguments.\n");
                print_help();
                return 0;
            case 'h':
                print_help();
                break;
            default:
                break;
        }
    }

    const char *exe_file_name = argv[optind];

    if (verbose) {
        printf("Parsing executable file '%s'...\n", exe_file_name);
    }

    // TODO implement default value where exe object decides internally what to do.
    unassemblize::Executable::OutputFormats format = unassemblize::Executable::OUTPUT_IGAS;

    if (format_string != nullptr) {
        if (strcasecmp(format_string, "igas") == 0) {
            format = unassemblize::Executable::OUTPUT_IGAS;
        } else if (strcasecmp(format_string, "masm") == 0) {
            format = unassemblize::Executable::OUTPUT_MASM;
        }
    }

    unassemblize::Executable exe(exe_file_name, format, verbose);

    if (print_secs) {
        print_sections(exe);
        return 0;
    }

    if (dump_syms) {
        exe.save_config(config_file);
        return 0;
    }

    exe.load_config(config_file);

    if (start_addr == 0 && end_addr == 0) {
        for (const unassemblize::Executable::SymbolMap::value_type &pair : exe.get_symbol_map()) {
            const uint64_t address = pair.first;
            const unassemblize::Executable::Symbol &symbol = pair.second;
            std::string sanitized_symbol_name = symbol.name;
#if defined(WIN32)
            remove_characters(sanitized_symbol_name, "\\/:*?\"<>|");
#endif
            std::string function_file_name = std::string(exe_file_name) + "." + sanitized_symbol_name + ".S";
            dump_function_to_file(
                function_file_name.c_str(), exe, section_name, symbol.address, symbol.address + symbol.size);
        }
    } else {
        dump_function_to_file(output, exe, section_name, start_addr, end_addr);
    }

    return 0;
}
