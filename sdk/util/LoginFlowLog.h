/**
 * @file    LoginFlowLog.h
 * @brief   登录全链路结构化日志字段约定（phase/accid/userId/conn/code）
 *
 * 各服在关键节点调用，便于 grep 与后续指标采集对齐。
 * 告警阈值见 LOGIN_FLOW_ALERT_* 常量。
 */

#pragma once

#include "../log/Logger.h"

#include <cstdint>

/** @brief 单用户登录事务在 Super 侧最大等待时长（与 LoginSpawnConfig 一致） */
constexpr uint64_t LOGIN_FLOW_TXN_TIMEOUT_MS = 60000;

/** @brief 连续登录失败次数告警阈值（单 conn，需业务侧累计） */
constexpr uint32_t LOGIN_FLOW_ALERT_FAIL_STREAK = 5;

/** @brief 待完成登录堆积告警阈值（Super m_pendingLogins 规模） */
constexpr uint32_t LOGIN_FLOW_ALERT_PENDING_COUNT = 100;

/**
 * @brief 登录链路阶段标识（写入日志 phase 字段）
 */
enum class LoginFlowPhase : uint8_t
{
    ACCOUNT_LOGIN = 0,  /**< LoginServer 账号登录 */
    GATEWAY_AUTH  = 1,  /**< Gateway 票据鉴权 */
    CHAR_LIST     = 2,  /**< 角色列表 */
    CHAR_CREATE   = 3,  /**< 创角 */
    CHAR_SELECT   = 4,  /**< 选角 */
    SUPER_ENTER   = 5,  /**< Super 进世界编排 */
    SCENE_ENTER   = 6,  /**< Scene 入场完成 */
    CHAR_LEAVE    = 7,  /**< 主动离世界（Scene/Super 清理） */
    LOGOUT        = 8,  /**< 客户端退出意图（回选角/回登录） */
};

/** @brief 阶段名（中文，用于日志） */
inline const char* loginFlowPhaseName(LoginFlowPhase phase)
{
    switch (phase)
    {
    case LoginFlowPhase::ACCOUNT_LOGIN: return "账号登录";
    case LoginFlowPhase::GATEWAY_AUTH:  return "网关鉴权";
    case LoginFlowPhase::CHAR_LIST:     return "角色列表";
    case LoginFlowPhase::CHAR_CREATE:   return "创角";
    case LoginFlowPhase::CHAR_SELECT:   return "选角";
    case LoginFlowPhase::SUPER_ENTER:   return "超级服进世界";
    case LoginFlowPhase::SCENE_ENTER:     return "场景入场";
    case LoginFlowPhase::CHAR_LEAVE:      return "角色离世界";
    case LoginFlowPhase::LOGOUT:          return "退出登录";
    default: return "未知";
    }
}

/**
 * @brief 输出统一格式登录链路日志
 * @param phase 阶段
 * @param accid 账号 ID（无则 0）
 * @param userId 角色 ID（无则 0）
 * @param connId 客户端或网关 conn（无则 0）
 * @param code 0=成功，非 0=错误码
 * @param detail 补充说明（可为空）
 * @param loginTxnId 登录事务幂等键（无则 0）
 */
inline void logLoginFlow(LoginFlowPhase phase, uint64_t accid, uint64_t userId,
                         uint32_t connId, int32_t code, const char* detail = nullptr,
                         uint64_t loginTxnId = 0)
{
    if (code == 0)
    {
        LOG_INFO("[登录链路] phase=%s accid=%llu userId=%llu conn=%u txn=%llu code=%d%s%s",
                 loginFlowPhaseName(phase),
                 static_cast<unsigned long long>(accid),
                 static_cast<unsigned long long>(userId),
                 connId,
                 static_cast<unsigned long long>(loginTxnId),
                 code,
                 detail ? " " : "",
                 detail ? detail : "");
    }
    else
    {
        LOG_WARN("[登录链路] phase=%s accid=%llu userId=%llu conn=%u txn=%llu code=%d%s%s",
                 loginFlowPhaseName(phase),
                 static_cast<unsigned long long>(accid),
                 static_cast<unsigned long long>(userId),
                 connId,
                 static_cast<unsigned long long>(loginTxnId),
                 code,
                 detail ? " " : "",
                 detail ? detail : "");
    }
}
