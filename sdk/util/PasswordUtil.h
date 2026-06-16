/**
 * @file    PasswordUtil.h
 * @brief   密码哈希与校验工具（bcrypt）
 */

#pragma once

#include <crypt.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <random>

/**
 * @brief 生成 bcrypt 密码哈希
 * @param plain   明文密码
 * @param outHash [out] 生成的 bcrypt 哈希串
 * @return 成功返回 true
 */
bool hashPasswordBcrypt(const std::string& plain, std::string& outHash);

/**
 * @brief 校验明文密码与 bcrypt 哈希是否匹配
 * @param plain 明文密码
 * @param hash  存库哈希
 * @return 匹配返回 true
 */
bool verifyPasswordBcrypt(const std::string& plain, const std::string& hash);

namespace password_util_detail
{
constexpr char BCRYPT_BASE64[] =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
constexpr size_t BCRYPT_SALT_RAW_LEN = 16;
constexpr int BCRYPT_COST = 12;

inline std::string buildBcryptSalt()
{
    std::array<uint8_t, BCRYPT_SALT_RAW_LEN> raw{};
    std::random_device rd;
    for (size_t i = 0; i < raw.size(); ++i)
        raw[i] = static_cast<uint8_t>(rd());

    std::string encoded;
    encoded.reserve(22);
    uint32_t acc = 0;
    int bits = 0;
    for (uint8_t b : raw)
    {
        acc = (acc << 8) | b;
        bits += 8;
        while (bits >= 6)
        {
            bits -= 6;
            encoded.push_back(BCRYPT_BASE64[(acc >> bits) & 0x3F]);
        }
    }
    if (bits > 0)
        encoded.push_back(BCRYPT_BASE64[(acc << (6 - bits)) & 0x3F]);
    if (encoded.size() > 22)
        encoded.resize(22);
    while (encoded.size() < 22)
        encoded.push_back('.');

    char prefix[8];
    std::snprintf(prefix, sizeof(prefix), "$2b$%02d$", BCRYPT_COST);
    return std::string(prefix) + encoded;
}
} // namespace password_util_detail

inline bool hashPasswordBcrypt(const std::string& plain, std::string& outHash)
{
    if (plain.empty())
        return false;
    const std::string salt = password_util_detail::buildBcryptSalt();
    char* hashed = ::crypt(plain.c_str(), salt.c_str());
    if (!hashed)
        return false;
    outHash.assign(hashed);
    return !outHash.empty();
}

inline bool verifyPasswordBcrypt(const std::string& plain, const std::string& hash)
{
    if (plain.empty() || hash.empty())
        return false;
    char* check = ::crypt(plain.c_str(), hash.c_str());
    if (!check)
        return false;
    return hash == check;
}
