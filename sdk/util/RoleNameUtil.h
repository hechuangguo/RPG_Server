/**
 * @file    RoleNameUtil.h
 * @brief   创角角色名 UTF-8 校验（中文/英文混排，Gateway 与 Record 共用）
 *
 * 规则：2–12 个 Unicode 码点；UTF-8 字节数 ≤ MAX_ROLE_NAME_BYTES；
 * 允许 ASCII 字母/数字/下划线与 CJK 统一汉字（U+4E00–U+9FFF）。
 */

#pragma once

#include <cstdint>

/** @brief 角色名最少码点数 */
constexpr uint32_t MIN_ROLE_NAME_CHAR_COUNT = 2;

/** @brief 角色名最多码点数 */
constexpr uint32_t MAX_ROLE_NAME_CHAR_COUNT = 12;

/** @brief wire/DB 角色名最大 UTF-8 字节数 */
constexpr uint32_t MAX_ROLE_NAME_BYTES = 31;

/**
 * @brief 校验创角角色名（UTF-8）
 * @param name 以 \\0 结尾的 UTF-8 字符串
 * @return 合法 true
 */
bool isValidRoleNameUtf8(const char* name);
