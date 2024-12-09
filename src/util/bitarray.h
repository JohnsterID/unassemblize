/**
 * @file
 *
 * @brief Array with bits
 *
 * @copyright Unassemblize is free software: you can redistribute it and/or
 *            modify it under the terms of the GNU General Public License
 *            as published by the Free Software Foundation, either version
 *            3 of the License, or (at your option) any later version.
 *            A full copy of the GNU General Public License can be found in
 *            LICENSE
 */
#pragma once

#include <cstdint>
#include <memory>

class BitArray
{
public:
    BitArray() = default;

    BitArray(size_t size, bool defaultValue) : m_size(size)
    {
        const size_t bitsSize = m_size / 8 + 1;
        m_bitArray = std::make_unique<uint8_t[]>(bitsSize);
        std::fill_n(m_bitArray.get(), bitsSize, defaultValue);
    }

    void set(size_t index)
    {
        const size_t bitIndex = index / 8;
        const uint8_t bitField = (1 << (index % 8));
        m_bitArray[bitIndex] |= bitField;
    }

    void unset(size_t index)
    {
        const size_t bitIndex = index / 8;
        const uint8_t bitField = (1 << (index % 8));
        m_bitArray[bitIndex] &= ~bitField;
    }

    bool get(size_t index) const
    {
        const size_t bitIndex = index / 8;
        const uint8_t bitField = (1 << (index % 8));
        return (m_bitArray[bitIndex] & bitField) != uint8_t(0);
    }

    size_t size() const { return m_size; }

private:
    std::unique_ptr<uint8_t[]> m_bitArray;
    size_t m_size = 0;
};
