#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace MemMapper
{
class SparseReservation
{
public:
    struct Range { uint64_t begin, end; };

    template<typename Reserve, typename Release>
    bool Ensure(uintptr_t base, uint64_t offset, uint64_t size, size_t pageSize,
                Reserve reserve, Release release)
    {
        constexpr uint64_t addressSpaceSize = uint64_t{1} << 32;
        if (!size || !pageSize || (pageSize & (pageSize - 1)) ||
            offset >= addressSpaceSize || size > addressSpaceSize - offset ||
            base > UINTPTR_MAX - addressSpaceSize)
            return false;
        const uint64_t begin = offset & ~(uint64_t(pageSize) - 1);
        const uint64_t end = (offset + size + pageSize - 1) & ~(uint64_t(pageSize) - 1);
        std::vector<Range> missing;
        uint64_t cursor = begin;
        for (const auto& range : m_ranges)
        {
            if (range.end <= cursor) continue;
            if (range.begin >= end) break;
            if (range.begin > cursor) missing.push_back({cursor, range.begin});
            cursor = std::max(cursor, range.end);
        }
        if (cursor < end) missing.push_back({cursor, end});

        size_t acquired = 0;
        for (const auto& range : missing)
        {
            if (!reserve(base + range.begin, range.end - range.begin))
            {
                for (size_t i = 0; i < acquired; ++i)
                    release(base + missing[i].begin, missing[i].end - missing[i].begin);
                return false;
            }
            ++acquired;
        }
        m_ranges.insert(m_ranges.end(), missing.begin(), missing.end());
        std::sort(m_ranges.begin(), m_ranges.end(), [](const Range& a, const Range& b) { return a.begin < b.begin; });
        return true;
    }

    template<typename Release>
    void ReleaseAll(uintptr_t base, Release release)
    {
        for (const auto& range : m_ranges)
            release(base + range.begin, range.end - range.begin);
        m_ranges.clear();
    }

private:
    std::vector<Range> m_ranges;
};
}
