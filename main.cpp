/**
 * @file
 *
 * @brief main function and option handling.
 *
 * @copyright Unassemblize is free software: you can redistribute it and/or
 *            modify it under the terms of the GNU General Public License
 *            as published by the Free Software Foundation, either version
 *            3 of the License, or (at your option) any later version.
 *            A full copy of the GNU General Public License can be found in
 *            LICENSE
 */
#include "gitinfo.h"
#include "runner.h"
#include "util.h"
#include <assert.h>
#include <cxxopts.hpp>
#include <iostream>
#include <stdio.h>
#include <strings.h>

void print_version()
{
    char revision[12] = {0};
    const char *version = GitTag[0] == 'v' ? GitTag : GitShortSHA1;

    if (GitTag[0] != 'v') {
        snprintf(revision, sizeof(revision), "r%d ", GitRevision);
    }

    printf("unassemblize %s%s%s by The Assembly Armada\n", revision, GitUncommittedChanges ? "~" : "", version);
}

const char *const auto_str = "auto"; // When output is set to "auto", then output name is chosen for input file name.

enum class InputType
{
    Unknown,
    Exe,
    Pdb,
};

std::string get_config_file_name(const std::string &input_file, const std::string &config_file)
{
    if (0 == strcasecmp(config_file.c_str(), auto_str)) {
        // program.config.json
        return util::get_file_name_without_ext(input_file) + ".config.json";
    }
    return config_file;
}

std::string get_output_file_name(const std::string &input_file, const std::string &output_file)
{
    if (0 == strcasecmp(output_file.c_str(), auto_str)) {
        // program.S
        return util::get_file_name_without_ext(input_file) + ".S";
    }
    return output_file;
}

InputType get_input_type(const std::string &input_file, const std::string &input_type)
{
    InputType type = InputType::Unknown;

    if (0 == strcasecmp(input_type.c_str(), auto_str)) {
        std::string input_file_ext = util::get_file_ext(input_file);
        if (0 == strcasecmp(input_file_ext.c_str(), "pdb")) {
            type = InputType::Pdb;
        } else {
            type = InputType::Exe;
        }
    } else if (0 == strcasecmp(input_type.c_str(), "exe")) {
        type = InputType::Exe;
    } else if (0 == strcasecmp(input_type.c_str(), "pdb")) {
        type = InputType::Pdb;
    }

    return type;
}

int main(int argc, char **argv)
{
    print_version();

    cxxopts::Options options("unassemblize", "x86 Unassembly tool");

#define OPT_INPUT "input"
#define OPT_INPUTTYPE "input-type"
#define OPT_OUTPUT "output"
#define OPT_FORMAT "format"
#define OPT_CONFIG "config"
#define OPT_START "start"
#define OPT_END "end"
#define OPT_LISTSECTIONS "list-sections"
#define OPT_DUMPSYMS "dumpsyms"
#define OPT_VERBOSE "verbose"
#define OPT_HELP "help"

    // clang-format off
    options.add_options("main", {
        cxxopts::Option{"i," OPT_INPUT, "Input file", cxxopts::value<std::string>()},
        cxxopts::Option{OPT_INPUTTYPE, "Input file type. Default is 'auto'", cxxopts::value<std::string>()},
        cxxopts::Option{"o," OPT_OUTPUT, "Filename for single file output. Default is 'auto'", cxxopts::value<std::string>()},
        cxxopts::Option{"f," OPT_FORMAT, "Assembly output format. Default is 'auto'", cxxopts::value<std::string>()},
        cxxopts::Option{"c," OPT_CONFIG, "Configuration file describing how to disassemble the input file and containing extra symbol info. Default is 'auto'", cxxopts::value<std::string>()},
        cxxopts::Option{"s," OPT_START, "Starting address of a single function to disassemble in hexadecimal notation.", cxxopts::value<std::string>()},
        cxxopts::Option{"e," OPT_END, "Ending address of a single function to disassemble in hexadecimal notation.", cxxopts::value<std::string>()},
        cxxopts::Option{OPT_LISTSECTIONS, "Prints a list of sections in the executable then exits."},
        cxxopts::Option{"d," OPT_DUMPSYMS, "Dumps symbols stored in a executable or pdb to the config file."},
        cxxopts::Option{"v," OPT_VERBOSE, "Verbose output on current state of the program."},
        cxxopts::Option{"h," OPT_HELP, "Displays this help."},
        });
    // clang-format on

    options.parse_positional({"input"});

    cxxopts::ParseResult result;
    try {
        result = options.parse(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }

    if (result.count("help") != 0) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    std::string input_file;
    // When input_file_type is set to "auto", then input file type is chosen by file extension.
    std::string input_type = auto_str;
    // When output_file is set to "auto", then output file name is chosen for input file name.
    std::string output_file = auto_str;
    std::string format_string = auto_str;
    // When config file is set to "auto", then config file name is chosen for input file name.
    std::string config_file = auto_str;
    uint64_t start_addr = 0x00000000;
    uint64_t end_addr = 0x00000000;
    bool print_secs = false;
    bool dump_syms = false;
    bool verbose = false;

    for (const cxxopts::KeyValue &kv : result.arguments()) {
        const std::string &v = kv.key();
        if (v == OPT_INPUT) {
            input_file = kv.value();
        } else if (v == OPT_INPUTTYPE) {
            input_type = kv.value();
        } else if (v == OPT_OUTPUT) {
            output_file = kv.value();
        } else if (v == OPT_FORMAT) {
            format_string = kv.value();
        } else if (v == OPT_CONFIG) {
            config_file = kv.value();
        } else if (v == OPT_START) {
            start_addr = strtoull(kv.value().c_str(), nullptr, 16);
        } else if (v == OPT_END) {
            end_addr = strtoull(kv.value().c_str(), nullptr, 16);
        } else if (v == OPT_LISTSECTIONS) {
            print_secs = kv.as<bool>();
        } else if (v == OPT_DUMPSYMS) {
            dump_syms = kv.as<bool>();
        } else if (v == OPT_VERBOSE) {
            verbose = kv.as<bool>();
        }
    }

    if (input_file.empty()) {
        printf("Missing input file command line argument. Exiting...\n");
        return 1;
    }

    const InputType type = get_input_type(input_file, input_type);

    if (InputType::Exe == type) {
        unassemblize::Runner runner;
        unassemblize::ExeOptions o;
        o.input_file = input_file;
        o.config_file = get_config_file_name(o.input_file, config_file);
        o.output_file = get_output_file_name(o.input_file, output_file);
        o.format_str = format_string;
        o.start_addr = start_addr;
        o.end_addr = end_addr;
        o.print_secs = print_secs;
        o.dump_syms = dump_syms;
        o.verbose = verbose;
        return runner.process_exe(o) ? 0 : 1;
    } else if (InputType::Pdb == type) {
        unassemblize::Runner runner;
        bool success;
        {
            unassemblize::PdbOptions o;
            o.input_file = input_file;
            o.config_file = get_config_file_name(o.input_file, config_file);
            o.print_secs = print_secs;
            o.dump_syms = dump_syms;
            o.verbose = verbose;
            success = runner.process_pdb(o);
        }
        if (success) {
            unassemblize::ExeOptions o;
            o.input_file = runner.get_pdb_exe_file_name();
            o.config_file = get_config_file_name(o.input_file, config_file);
            o.output_file = get_output_file_name(o.input_file, output_file);
            o.format_str = format_string;
            o.start_addr = start_addr;
            o.end_addr = end_addr;
            o.print_secs = print_secs;
            o.dump_syms = dump_syms;
            o.verbose = verbose;
            success = runner.process_exe(o);
        }
        return success ? 0 : 1;
    } else {
        printf("Unrecognized input file type '%s'. Exiting...\n", input_type.c_str());
        return 1;
    }
}
