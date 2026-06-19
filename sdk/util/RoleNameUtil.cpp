/**
 * @file    RoleNameUtil.cpp
 * @brief   创角角色名 UTF-8 校验实现
 */

#include "RoleNameUtil.h"

#include <cstddef>
#include <cstring>

namespace
{

/** @brief CJK 统一汉字区（创角允许中文） */
constexpr uint32_t CJK_UNIFIED_BEGIN = 0x4E00u;
constexpr uint32_t CJK_UNIFIED_END   = 0x9FFFu;

/**
 * @brief 解码 UTF-8 下一码点
 * @param s 当前指针
 * @param remaining 剩余字节数
 * @param outCp 输出码点
 * @param outConsumed 本码点消耗字节数
 * @return 成功 true
 */
bool decodeUtf8Codepoint(const char* s, size_t remaining, uint32_t& outCp, size_t& outConsumed)
{
    if (!s || remaining == 0)
        return false;

    const unsigned char b0 = static_cast<unsigned char>(s[0]);
    if (b0 < 0x80)
    {
        outCp = b0;
        outConsumed = 1;
        return true;
    }
    if ((b0 & 0xE0) == 0xC0)
    {
        if (remaining < 2 || (static_cast<unsigned char>(s[1]) & 0xC0) != 0x80)
            return false;
        outCp = ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(s[1]) & 0x3F);
        if (outCp < 0x80)
            return false;
        outConsumed = 2;
        return true;
    }
    if ((b0 & 0xF0) == 0xE0)
    {
        if (remaining < 3 ||
            (static_cast<unsigned char>(s[1]) & 0xC0) != 0x80 ||
            (static_cast<unsigned char>(s[2]) & 0xC0) != 0x80)
            return false;
        outCp = ((b0 & 0x0F) << 12) |
                ((static_cast<unsigned char>(s[1]) & 0x3F) << 6) |
                (static_cast<unsigned char>(s[2]) & 0x3F);
        if (outCp < 0x800)
            return false;
        outConsumed = 3;
        return true;
    }
    return false;
}

/** @brief 单码点是否在允许字符集内 */
bool isAllowedRoleNameCodepoint(uint32_t cp)
{
    if (cp <= 0x7Fu)
    {
        if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
            (cp >= '0' && cp <= '9') || cp == '_')
            return true;
        return false;
    }
    return cp >= CJK_UNIFIED_BEGIN && cp <= CJK_UNIFIED_END;
}

} // namespace

bool isValidRoleNameUtf8(const char* name)
{
    if (!name || name[0] == '\0')
        return false;

    const size_t byteLen = std::strlen(name);
    if (byteLen > MAX_ROLE_NAME_BYTES)
        return false;

    uint32_t charCount = 0;
    size_t offset = 0;
    while (offset < byteLen)
    {
        uint32_t cp = 0;
        size_t consumed = 0;
        if (!decodeUtf8Codepoint(name + offset, byteLen - offset, cp, consumed))
            return false;
        if (!isAllowedRoleNameCodepoint(cp))
            return false;
        ++charCount;
        offset += consumed;
    }

    return charCount >= MIN_ROLE_NAME_CHAR_COUNT && charCount <= MAX_ROLE_NAME_CHAR_COUNT;
}
