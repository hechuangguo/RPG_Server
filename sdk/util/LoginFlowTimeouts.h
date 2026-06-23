/**
 * @file    LoginFlowTimeouts.h
 * @brief   登录链路各阶段超时常量（Gateway / Record / Super 共用）
 */

#pragma once

#include <cstdint>

/** @brief Super→Login 外联 TLS 预热时间（重连后暂缓票据校验） */
constexpr uint64_t LOGIN_CONN_WARMUP_MS = 1500;

/** @brief Record 等待 Login 校验回包超时 */
constexpr uint64_t VERIFY_TOKEN_TIMEOUT_MS = 15000;

/** @brief Super 延后转发票据校验队列超时（须 ≥ Record 等待时长） */
constexpr uint64_t DEFERRED_VERIFY_TIMEOUT_MS = VERIFY_TOKEN_TIMEOUT_MS + 2000;

/** @brief Gateway 连接后未发起鉴权告警/踢线阈值 */
constexpr uint64_t GATEWAY_AUTH_TIMEOUT_MS = 10000;

/** @brief Gateway AUTHING 态最长等待（对齐 Record 校验 + 缓冲） */
constexpr uint64_t GATEWAY_AUTHING_TIMEOUT_MS = VERIFY_TOKEN_TIMEOUT_MS + 2000;

/** @brief Gateway 鉴权/心跳超时检测轮询间隔（与阈值独立，宜 ≤1s） */
constexpr uint64_t GATEWAY_TIMEOUT_POLL_MS = 1000;

/** @brief 外联 TLS 半开（已连接但不可写）强制断开阈值 */
constexpr uint64_t EXTERNAL_TLS_STUCK_MS = 3000;
