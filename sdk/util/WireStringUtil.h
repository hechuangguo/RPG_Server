/**
 * @file    WireStringUtil.h
 * @brief  协议定长字符串安全拷贝（替代 strncpy，消除 -Wstringop-truncation）
 */

#pragma once
#include <cstring>
#include <cstddef>

/** @brief C 字符串写入定长 wire 缓冲区（最多 dstSize-1 字节，末尾强制 \\0） */
inline void copyToWire(char* dst, size_t dstSize, const char* src)
{
    if (!dst || dstSize == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    const size_t maxCopy = dstSize - 1;
    size_t len = 0;
    while (len < maxCopy && src[len] != '\0')
        ++len;
    if (len > 0)
        std::memcpy(dst, src, len);
    dst[len] = '\0';
}

/** @brief 定长 wire 字段 → 定长 wire 字段（memcpy + 末字节 \\0） */
inline void copyWireField(char* dst, size_t dstSize,
                          const char* src, size_t srcSize)
{
    if (!dst || dstSize == 0)
        return;
    if (!src || srcSize == 0)
    {
        dst[0] = '\0';
        return;
    }
    const size_t maxCopy = (dstSize < srcSize ? dstSize : srcSize) - 1;
    if (maxCopy > 0)
        std::memcpy(dst, src, maxCopy);
    dst[maxCopy] = '\0';
}

/** @brief 定长数组便捷重载 */
template <size_t N, size_t M>
inline void copyWireField(char (&dst)[N], const char (&src)[M])
{
    copyWireField(dst, N, src, M);
}
