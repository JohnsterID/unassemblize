/**
 * @file
 *
 * @brief ImGui utility
 *
 * @copyright Unassemblize is free software: you can redistribute it and/or
 *            modify it under the terms of the GNU General Public License
 *            as published by the Free Software Foundation, either version
 *            3 of the License, or (at your option) any later version.
 *            A full copy of the GNU General Public License can be found in
 *            LICENSE
 */
#pragma once

#include <functional>
#include <imgui.h>
#include <string_view>

struct ImGuiTextFilterEx : public ImGuiTextFilter
{
    bool Draw(const char *key, const char *label = "Filter (inc,-exc)", float width = 0.0f);
    bool PassFilter(std::string_view view) const;

    bool hasExternalFilterCondition = false; // Set true if the filter callback has more than just the text to filter with.
};

namespace unassemblize::gui
{
template<typename Type>
struct TextFilterDescriptor
{
    using FilterType = Type;

    TextFilterDescriptor(const char *key) : key(key) {}

    // Clears the filtered state but does not reset the user specified filter words.
    void reset()
    {
        filtered.clear();
        filteredOnce = false;
    }

    void set_external_filter_condition(bool value) { filter.hasExternalFilterCondition = value; }
    bool needs_update() const { return !filteredOnce; }

    const char *const key;
    ImGuiTextFilterEx filter; // The filter state for the input field.
    ImVector<FilterType> filtered; // Array to the filtered elements after an update.
    bool filteredOnce = false; // Set true to force the next draw to update the filtered state.
};

template<typename SourceContainerValueType>
using FilterCallback = std::function<bool(const ImGuiTextFilterEx &filter, const SourceContainerValueType &value)>;

template<typename FilterType, typename Container>
void UpdateFilter(
    ImVector<FilterType> &filtered,
    const ImGuiTextFilterEx &filter,
    const Container &source,
    FilterCallback<typename Container::value_type> &&filterCallback)
{
    if (filter.IsActive() || filter.hasExternalFilterCondition)
    {
        const size_t size = source.size();
        filtered.clear();
        filtered.reserve(size);
        for (size_t i = 0; i < size; ++i)
        {
            if (filterCallback(filter, source[i]))
            {
                if constexpr (std::is_pointer_v<FilterType>)
                    filtered.push_back(&source[i]);
                else
                    filtered.push_back(source[i]);
            }
        }
    }
    else
    {
        const size_t size = source.size();
        filtered.resize(size);
        for (size_t i = 0; i < size; ++i)
        {
            if constexpr (std::is_pointer_v<FilterType>)
                filtered[i] = &source[i];
            else
                filtered[i] = source[i];
        }
    }
}

template<typename Descriptor>
bool DrawFilter(Descriptor &descriptor)
{
    return descriptor.filter.Draw(descriptor.key) || descriptor.needs_update();
}

template<typename Descriptor, typename Container>
void UpdateFilter(
    Descriptor &descriptor,
    const Container &source,
    FilterCallback<typename Container::value_type> &&filterCallback)
{
    using FilterType = typename Descriptor::FilterType;
    UpdateFilter<FilterType>(descriptor.filtered, descriptor.filter, source, std::move(filterCallback));
    descriptor.filteredOnce = true;
}

template<typename Descriptor, typename Container>
bool DrawAndUpdateFilter(
    Descriptor &descriptor,
    const Container &source,
    FilterCallback<typename Container::value_type> &&filterCallback)
{
    if (DrawFilter(descriptor))
    {
        UpdateFilter(descriptor, source, std::move(filterCallback));
        return true;
    }
    return false;
}

} // namespace unassemblize::gui
