#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

class AudioRingBuffer final
{
public:
    explicit AudioRingBuffer(size_t capacity)
    : m_storage(std::make_unique<std::uint8_t[]>(capacity)), m_capacity(capacity) {}

    AudioRingBuffer(const AudioRingBuffer&) = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;

    bool write(const std::uint8_t* data, size_t size)
    {
        if (size == 0 || size > m_capacity)
            return size == 0;

        const size_t writePosition = m_write.load(std::memory_order_relaxed);
        const size_t readPosition = m_read.load(std::memory_order_acquire);
        if (size > m_capacity - (writePosition - readPosition))
            return false;

        copyIn(writePosition, data, size);
        m_write.store(writePosition + size, std::memory_order_release);
        return true;
    }

    size_t read(std::uint8_t* data, size_t maxSize)
    {
        if (maxSize == 0)
            return 0;

        const size_t readPosition = m_read.load(std::memory_order_relaxed);
        const size_t writePosition = m_write.load(std::memory_order_acquire);
        const size_t available = writePosition - readPosition;
        const size_t size = std::min(maxSize, available);
        if (size == 0)
            return 0;

        copyOut(readPosition, data, size);
        m_read.store(readPosition + size, std::memory_order_release);
        return size;
    }

    size_t size() const
    {
        const size_t writePosition = m_write.load(std::memory_order_acquire);
        const size_t readPosition = m_read.load(std::memory_order_acquire);
        return writePosition - readPosition;
    }

private:
    void copyIn(size_t position, const std::uint8_t* data, size_t size)
    {
        const size_t offset = position % m_capacity;
        const size_t firstSize = std::min(size, m_capacity - offset);
        std::memcpy(m_storage.get() + offset, data, firstSize);

        if (firstSize < size)
            std::memcpy(m_storage.get(), data + firstSize, size - firstSize);
    }

    void copyOut(size_t position, std::uint8_t* data, size_t size)
    {
        const size_t offset = position % m_capacity;
        const size_t firstSize = std::min(size, m_capacity - offset);
        std::memcpy(data, m_storage.get() + offset, firstSize);
        if (firstSize < size)
            std::memcpy(data + firstSize, m_storage.get(), size - firstSize);
    }

    std::unique_ptr<std::uint8_t[]> m_storage;
    size_t m_capacity;
    std::atomic<size_t> m_read{0};
    std::atomic<size_t> m_write{0};
};
