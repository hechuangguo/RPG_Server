/**
 * @file    PasswordDigestUtil.h
 * @brief   登录 wire SHA-256 摘要与 bcrypt 存库校验（方案 1）
 *
 * 客户端发送 32 字节 SHA-256(UTF-8 密码)；服务端存 bcrypt(hex(digest))。
 */

#pragma once

#include "PasswordUtil.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

/** @brief SHA-256 摘要字节数（与 LoginMsg passwordDigest 字段一致） */
constexpr size_t PASSWORD_DIGEST_LEN = 32;

/**
 * @brief 32 字节 digest 转为 64 字符小写 hex（bcrypt 输入）
 */
inline std::string digestToHex(const uint8_t digest[PASSWORD_DIGEST_LEN])
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.resize(PASSWORD_DIGEST_LEN * 2);
    for (size_t i = 0; i < PASSWORD_DIGEST_LEN; ++i)
    {
        out[i * 2]     = hex[(digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    return out;
}

/**
 * @brief digest 是否全零（非法）
 */
inline bool isZeroDigest(const uint8_t digest[PASSWORD_DIGEST_LEN])
{
    for (size_t i = 0; i < PASSWORD_DIGEST_LEN; ++i)
    {
        if (digest[i] != 0)
            return false;
    }
    return true;
}

/**
 * @brief 启发式：是否像 legacy 明文（可打印 ASCII 且非满 32 字节二进制）
 * @param digest wire 字段 32 字节
 * @return 若前若干字节为可打印 ASCII 且尾部为 0 填充，视为明文
 */
inline bool looksLikePlaintextPassword(const uint8_t digest[PASSWORD_DIGEST_LEN])
{
    size_t len = 0;
    while (len < PASSWORD_DIGEST_LEN && digest[len] != 0)
        ++len;
    if (len == 0 || len >= PASSWORD_DIGEST_LEN)
        return false;
    for (size_t i = 0; i < len; ++i)
    {
        const unsigned char ch = digest[i];
        if (ch < 33 || ch > 126)
            return false;
    }
    for (size_t i = len; i < PASSWORD_DIGEST_LEN; ++i)
    {
        if (digest[i] != 0)
            return false;
    }
    return true;
}

/**
 * @brief 校验 wire digest 与存库 bcrypt(hex(digest))
 */
inline bool verifyPasswordDigestBcrypt(const uint8_t digest[PASSWORD_DIGEST_LEN],
                                       const std::string& storedHash)
{
    if (isZeroDigest(digest))
        return false;
    const std::string hex = digestToHex(digest);
    return verifyPasswordBcrypt(hex, storedHash);
}

/**
 * @brief 注册时 bcrypt(hex(digest)) 写入 GameUser.password_hash
 */
inline bool hashPasswordDigestBcrypt(const uint8_t digest[PASSWORD_DIGEST_LEN],
                                     std::string& outHash)
{
    if (isZeroDigest(digest))
        return false;
    return hashPasswordBcrypt(digestToHex(digest), outHash);
}

/**
 * @brief 比较两个 wire digest 是否一致（注册确认密码）
 */
inline bool digestsEqual(const uint8_t a[PASSWORD_DIGEST_LEN],
                         const uint8_t b[PASSWORD_DIGEST_LEN])
{
    return std::memcmp(a, b, PASSWORD_DIGEST_LEN) == 0;
}
