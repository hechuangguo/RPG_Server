#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>

// ============================================================
//  环形缓冲区
// ============================================================
class RingBuffer
{
public:
    explicit RingBuffer(uint32_t capacity = 65536)
        : m_capacity(capacity), m_readPos(0), m_writePos(0), m_size(0)
    {
        m_buf = new char[capacity];
    }
    ~RingBuffer() { delete[] m_buf; }

    // 可写空间
    uint32_t WritableBytes() const { return m_capacity - m_size; }
    // 可读数据量
    uint32_t ReadableBytes()  const { return m_size; }
    // 是否为空
    bool     Empty()          const { return m_size == 0; }

    // 写入数据
    bool Write(const char* data, uint32_t len)
    {
        if (len > WritableBytes()) return false;
        uint32_t first = m_capacity - m_writePos;
        if (first >= len)
        {
            std::memcpy(m_buf + m_writePos, data, len);
        }
        else
        {
            std::memcpy(m_buf + m_writePos, data, first);
            std::memcpy(m_buf, data + first, len - first);
        }
        m_writePos = (m_writePos + len) % m_capacity;
        m_size += len;
        return true;
    }

    // 读取数据（不移动读指针）
    bool Peek(char* out, uint32_t len) const
    {
        if (len > m_size) return false;
        uint32_t first = m_capacity - m_readPos;
        if (first >= len)
        {
            std::memcpy(out, m_buf + m_readPos, len);
        }
        else
        {
            std::memcpy(out, m_buf + m_readPos, first);
            std::memcpy(out + first, m_buf, len - first);
        }
        return true;
    }

    // 移动读指针
    void Consume(uint32_t len)
    {
        assert(len <= m_size);
        m_readPos = (m_readPos + len) % m_capacity;
        m_size -= len;
    }

    // 读取并移动
    bool Read(char* out, uint32_t len)
    {
        if (!Peek(out, len)) return false;
        Consume(len);
        return true;
    }

    void Clear() { m_readPos = m_writePos = m_size = 0; }

private:
    char*    m_buf;
    uint32_t m_capacity;
    uint32_t m_readPos;
    uint32_t m_writePos;
    uint32_t m_size;
};
