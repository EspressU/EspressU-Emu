#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

class MetalSharedBufferTracker
{
public:
    void Initialize(size_t size)
    {
        m_size = size;
        m_pages.resize((size + PageSize - 1) / PageSize);
    }

    bool Busy(size_t offset, size_t size, bool writesOnly = false) const
    {
        if (offset >= m_size || size == 0)
            return false;
        const size_t last = (offset + std::min(size, m_size - offset) - 1) / PageSize;
        for (size_t page = offset / PageSize; page <= last; ++page)
            if ((writesOnly ? m_pages[page].write : m_pages[page].use) > m_completed)
                return true;
        return false;
    }

    void Mark(const void* command, size_t offset, size_t size, bool write = false)
    {
        if (offset >= m_size || size == 0)
            return;
        assert(command);
        if (command != m_currentCommand)
        {
            m_currentCommand = command;
            m_commands.emplace(command, ++m_serial);
        }
        const size_t last = (offset + std::min(size, m_size - offset) - 1) / PageSize;
        for (size_t page = offset / PageSize; page <= last; ++page)
        {
            m_pages[page].use = m_serial;
            if (write)
                m_pages[page].write = m_serial;
        }
    }

    void Complete(const void* command)
    {
        const auto it = m_commands.find(command);
        if (it == m_commands.end())
            return;
        m_completed = std::max(m_completed, it->second);
        m_commands.erase(it);
        if (command == m_currentCommand)
            m_currentCommand = nullptr;
    }

private:
    static constexpr size_t PageSize = 1024;
    struct Page { uint64_t use = 0; uint64_t write = 0; };
    size_t m_size = 0;
    uint64_t m_serial = 0;
    uint64_t m_completed = 0;
    const void* m_currentCommand = nullptr;
    std::vector<Page> m_pages;
    std::unordered_map<const void*, uint64_t> m_commands;
};
