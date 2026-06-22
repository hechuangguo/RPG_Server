/**
 * @file    RingBuffer.h
 * @brief  无锁环形缓冲区（单生产者-单消费者）
 *
 * 内部使用裸指针 + 两个游标实现 O(1) 读写。
 * 适用于网络层接收/发送缓冲区场景，不依赖互斥锁，完全在单线程内使用。
 *
 * 存储模型：
 * @code
 *   [ ... readable ... | ... writable ... ]
 *   ^                  ^                  ^
 *   readPos            writePos           capacity
 * @endcode
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>

/**
 * @brief 环形字节缓冲区（单线程）
 *
 * 通过固定容量数组与读写游标实现无扩容、无锁的字节队列。
 */
class RingBuffer
{
public:
    /**
     * @brief 构造指定容量的环形缓冲区
     * @param capacity 总容量（字节），默认 65536
     */
    explicit RingBuffer(uint32_t capacity = 65536)
        : m_capacity(capacity), m_readPos(0), m_writePos(0), m_size(0)
    {
        m_buf = new char[capacity];
    }

    /** @brief 释放底层缓冲区内存 */
    ~RingBuffer() { delete[] m_buf; }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    /** @brief 剩余可写字节数 */
    uint32_t WritableBytes() const { return m_capacity - m_size; }

    /** @brief 已缓冲待读取字节数 */
    uint32_t ReadableBytes()  const { return m_size; }

    /** @brief 缓冲区是否为空 */
    bool     Empty()          const { return m_size == 0; }

    /**
     * @brief 写入数据（环内可能分两段拷贝）
     * @param data 源数据指针
     * @param len  写入字节数
     * @return 成功返回 true，空间不足返回 false
     */
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

    /**
     * @brief 查看数据但不移动读指针
     * @param out 目标缓冲区
     * @param len 期望读取字节数
     * @return 成功返回 true，数据不足返回 false
     * @note   与 Consume() 配合可实现零拷贝分包解析
     */
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

    /**
     * @brief 仅移动读指针（不拷贝数据）
     * @param len 跳过的字节数
     * @note  通常与 Peek() 配对使用：先 Peek 消息头确定长度，再 Consume 丢弃已解析数据
     */
    void Consume(uint32_t len)
    {
        if (len > m_size)
            return;
        m_readPos = (m_readPos + len) % m_capacity;
        m_size -= len;
    }

    /**
     * @brief 读取并移动读指针（= Peek + Consume）
     * @param out 目标缓冲区
     * @param len 读取字节数
     * @return 成功返回 true
     */
    bool Read(char* out, uint32_t len)
    {
        if (!Peek(out, len)) return false;
        Consume(len);
        return true;
    }

    /** @brief 清空缓冲区，重置所有游标 */
    void Clear() { m_readPos = m_writePos = m_size = 0; }

private:
    char*    m_buf;       /**< 底层内存块 */
    uint32_t m_capacity;  /**< 总容量（字节） */
    uint32_t m_readPos;   /**< 读游标（下一个可读字节的位置） */
    uint32_t m_writePos;  /**< 写游标（下一个可写字节的位置） */
    uint32_t m_size;      /**< 当前缓冲数据量（字节） */
};
