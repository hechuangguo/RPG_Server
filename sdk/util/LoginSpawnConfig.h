/**
 * @file    LoginSpawnConfig.h
 * @brief   新角色默认出生点与创角上限常量
 */

#pragma once

#include <cstdint>

/** @brief 新角色默认地图（新手村，与 server_info.xml Map 1001 对齐） */
constexpr uint32_t DEFAULT_NEWBIE_MAP_ID = 1001;

/** @brief 新手村默认出生坐标 */
constexpr float DEFAULT_NEWBIE_SPAWN_X = 100.f;
constexpr float DEFAULT_NEWBIE_SPAWN_Y = 0.f;
constexpr float DEFAULT_NEWBIE_SPAWN_Z = 100.f;

/** @brief 每账号每区最大角色数 */
constexpr uint32_t MAX_CHARACTERS_PER_ACCOUNT = 3;

/** @brief 职业 ID 上限（0=战士 1=法师 2=弓手 3=刺客，与 UserBaseWire 一致） */
constexpr uint8_t MAX_VOCATION_ID = 3;

/** @brief 性别 ID 上限（0=男 1=女） */
constexpr uint8_t MAX_SEX_ID = 1;

/** @brief 角色模型 ID 下限（1=男大） */
constexpr uint8_t MIN_MODEL_ID = 1;

/** @brief 角色模型 ID 上限（4=女小） */
constexpr uint8_t MAX_MODEL_ID = 4;

/** @brief LoginServer 下发给 Gateway 的 loginToken 有效期（秒） */
constexpr uint32_t LOGIN_TOKEN_TTL_SEC = 300;

/** @brief Super 登录事务锁超时（毫秒） */
constexpr uint64_t LOGIN_TXN_LOCK_TIMEOUT_MS = 60000;
