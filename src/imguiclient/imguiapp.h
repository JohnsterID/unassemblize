/**
 * @file
 *
 * @brief ImGui App core
 *
 * @copyright Unassemblize is free software: you can redistribute it and/or
 *            modify it under the terms of the GNU General Public License
 *            as published by the Free Software Foundation, either version
 *            3 of the License, or (at your option) any later version.
 *            A full copy of the GNU General Public License can be found in
 *            LICENSE
 */
#pragma once

#include "utility/imgui_misc.h"
#include "utility/imgui_text_filter.h"
#include <imgui.h>

#include "programcomparisondescriptor.h"
#include "programfiledescriptor.h"
#include "programfilerevisiondescriptor.h"

#include "runnerasync.h"

struct CommandLineOptions;

namespace unassemblize::gui
{
enum class ImGuiStatus
{
    Ok,
    Error,
};

class ImGuiApp
{
    // clang-format off
    static constexpr ImGuiTableFlags FileManagerInfoTableFlags =
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Hideable |
        ImGuiTableFlags_ContextMenuInBody |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersV |
        ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_NoHostExtendX |
        ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_ScrollY;
    // clang-format on

    static constexpr uint8_t GuiBuildBundleFlags = BuildMatchedFunctionIndices | BuildAllNamedFunctionIndices;
    static constexpr ImU32 RedColor = IM_COL32(255, 0, 0, 255);
    static constexpr ImU32 GreenColor = IM_COL32(0, 255, 0, 255);
    static constexpr std::chrono::system_clock::time_point InvalidTimePoint = std::chrono::system_clock::time_point::min();

    using ProgramFileDescriptorPair = std::array<ProgramFileDescriptor *, 2>;

public:
    ImGuiApp();
    ~ImGuiApp();

    ImGuiStatus init(const CommandLineOptions &clo);

    void prepare_shutdown_wait();
    void prepare_shutdown_nowait();
    bool can_shutdown() const; // Signals that this app can shutdown.
    void shutdown();

    void set_window_pos(ImVec2 pos) { m_windowPos = pos; }
    void set_window_size(ImVec2 size) { m_windowSize = size; }
    ImGuiStatus update();

    const ImVec4 &get_clear_color() const { return m_clearColor; }

private:
    void update_app();

    static WorkQueueCommandPtr create_load_command(ProgramFileRevisionDescriptorPtr &revisionDescriptor);
    static WorkQueueCommandPtr create_load_exe_command(ProgramFileRevisionDescriptorPtr &revisionDescriptor);
    static WorkQueueCommandPtr create_load_pdb_command(ProgramFileRevisionDescriptorPtr &revisionDescriptor);
    static WorkQueueCommandPtr create_load_pdb_and_exe_command(ProgramFileRevisionDescriptorPtr &revisionDescriptor);

    static WorkQueueCommandPtr create_save_exe_config_command(ProgramFileRevisionDescriptorPtr &revisionDescriptor);
    static WorkQueueCommandPtr create_save_pdb_config_command(ProgramFileRevisionDescriptorPtr &revisionDescriptor);

    static WorkQueueCommandPtr create_build_named_functions_command(ProgramFileRevisionDescriptorPtr &revisionDescriptor);
    static WorkQueueCommandPtr create_build_matched_functions_command(ProgramComparisonDescriptor *comparisonDescriptor);
    static WorkQueueCommandPtr create_build_bundles_from_compilands_command(ProgramComparisonDescriptor::File *file);
    static WorkQueueCommandPtr create_build_bundles_from_source_files_command(ProgramComparisonDescriptor::File *file);
    static WorkQueueCommandPtr create_build_single_bundle_command(
        ProgramComparisonDescriptor *comparisonDescriptor,
        size_t bundle_file_idx);

    static WorkQueueCommandPtr create_disassemble_selected_functions_command(
        ProgramFileRevisionDescriptorPtr &revisionDescriptor,
        span<const IndexT> namedFunctionIndices);

    static WorkQueueCommandPtr create_build_source_lines_for_selected_functions_command(
        ProgramFileRevisionDescriptorPtr &revisionDescriptor,
        span<const IndexT> namedFunctionIndices);

    static WorkQueueCommandPtr create_load_source_files_for_selected_functions_command(
        ProgramFileRevisionDescriptorPtr &revisionDescriptor,
        span<const IndexT> namedFunctionIndices);

    static WorkQueueCommandPtr create_process_selected_functions_command(
        ProgramFileRevisionDescriptorPtr &revisionDescriptor,
        span<const IndexT> namedFunctionIndices);

    static WorkQueueCommandPtr create_build_comparison_records_for_selected_functions_command(
        ProgramComparisonDescriptor *comparisonDescriptor,
        span<const IndexT> matchedFunctionIndices);

    ProgramFileDescriptor *get_program_file_descriptor(size_t program_file_idx);

    void load_async(ProgramFileDescriptor *descriptor);

    void save_config_async(ProgramFileDescriptor *descriptor);

    void load_and_init_comparison_async(
        ProgramFileDescriptorPair fileDescriptorPair,
        ProgramComparisonDescriptor *comparisonDescriptor);
    void init_comparison_async(ProgramComparisonDescriptor *comparisonDescriptor);
    void build_named_functions_async(ProgramComparisonDescriptor *comparisonDescriptor);
    void build_matched_functions_async(ProgramComparisonDescriptor *comparisonDescriptor);
    void build_bundled_functions_async(ProgramComparisonDescriptor *comparisonDescriptor);

    void process_named_functions_async(
        ProgramFileRevisionDescriptorPtr &revisionDescriptor,
        span<const IndexT> namedFunctionIndices);

    void process_matched_functions_async(
        ProgramComparisonDescriptor *comparisonDescriptor,
        span<const IndexT> matchedFunctionIndices);

    void process_named_and_matched_functions_async(
        ProgramComparisonDescriptor *comparisonDescriptor,
        span<const IndexT> matchedFunctionIndices);

    void add_file();
    void remove_file(size_t idx);
    void remove_all_files();

    void add_program_comparison();
    void update_closed_program_comparisons();

    void on_functions_interaction(ProgramComparisonDescriptor &descriptor, ProgramComparisonDescriptor::File &file);

    static std::string create_section_string(uint32_t section_index, const ExeSections *sections);
    static std::string create_time_string(std::chrono::time_point<std::chrono::system_clock> time_point);

    void BackgroundWindow();
    void FileManagerWindow(bool *p_open);
    void OutputManagerWindow(bool *p_open);
    void ComparisonManagerWindows();

    void FileManagerBody();
    void FileManagerDescriptor(ProgramFileDescriptor &descriptor, bool &erased);
    void FileManagerDescriptorExeFile(ProgramFileDescriptor &descriptor);
    void FileManagerDescriptorExeConfig(ProgramFileDescriptor &descriptor);
    void FileManagerDescriptorPdbFile(ProgramFileDescriptor &descriptor);
    void FileManagerDescriptorPdbConfig(ProgramFileDescriptor &descriptor);
    void FileManagerDescriptorActions(ProgramFileDescriptor &descriptor, bool &erased);
    void FileManagerDescriptorSaveLoadStatus(const ProgramFileRevisionDescriptor &descriptor);
    void FileManagerDescriptorLoadStatus(const ProgramFileRevisionDescriptor &descriptor);
    void FileManagerDescriptorSaveStatus(const ProgramFileRevisionDescriptor &descriptor);
    void FileManagerGlobalButtons();
    void FileManagerInfo(ProgramFileDescriptor &fileDescriptor, const ProgramFileRevisionDescriptor &revisionDescriptor);
    void FileManagerInfoExeSections(const ProgramFileRevisionDescriptor &descriptor);
    void FileManagerInfoExeSymbols(
        ProgramFileDescriptor &fileDescriptor,
        const ProgramFileRevisionDescriptor &revisionDescriptor);
    void FileManagerInfoPdbCompilands(const ProgramFileRevisionDescriptor &descriptor);
    void FileManagerInfoPdbSourceFiles(const ProgramFileRevisionDescriptor &descriptor);
    void FileManagerInfoPdbSymbols(
        ProgramFileDescriptor &fileDescriptor,
        const ProgramFileRevisionDescriptor &revisionDescriptor);
    void FileManagerInfoPdbFunctions(
        ProgramFileDescriptor &fileDescriptor,
        const ProgramFileRevisionDescriptor &revisionDescriptor);
    void FileManagerInfoPdbExeInfo(const ProgramFileRevisionDescriptor &descriptor);

    void OutputManagerBody();

    void ComparisonManagerBody(ProgramComparisonDescriptor &descriptor);
    void ComparisonManagerProgramFileSelection(ProgramComparisonDescriptor::File &file);
    void ComparisonManagerItemListStyleColor(
        ScopedStyleColor &styleColor,
        const ProgramComparisonDescriptor::File::ListItemUiInfo &uiInfo);

private:
    ImVec2 m_windowPos = ImVec2(0, 0);
    ImVec2 m_windowSize = ImVec2(0, 0);
    ImVec4 m_clearColor = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);

    bool m_showDemoWindow = true;
    bool m_showFileManager = true;
    bool m_showFileManagerWithTabs = false;
    bool m_showFileManagerExeSectionInfo = true;
    bool m_showFileManagerExeSymbolInfo = true;
    bool m_showFileManagerPdbCompilandInfo = true;
    bool m_showFileManagerPdbSourceFileInfo = true;
    bool m_showFileManagerPdbSymbolInfo = true;
    bool m_showFileManagerPdbFunctionInfo = true;
    bool m_showFileManagerPdbExeInfo = true;

    bool m_showOutputManager = true;

    WorkQueue m_workQueue;

    std::vector<ProgramFileDescriptorPtr> m_programFiles;
    std::vector<ProgramComparisonDescriptorPtr> m_programComparisons;
};

} // namespace unassemblize::gui
