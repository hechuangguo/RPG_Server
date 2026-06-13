/**
 * @file    SuperLoggerMsg.h
 * @brief   SuperServer Logger 外联消息注册
 *
 * Logger 业务（LOG_WRITE_REQ 等）由 SuperExternRouter 经 SS_EXTERN_FWD 统一转发。
 * 本模块为 Super 侧 Logger 专用 handler 注册占位，便于后续扩展（如统计、限流）。
 */

#pragma once

class SuperServer;

/**
 * @brief 注册 Logger 相关 Super 侧 handler（当前为占位，主路径见 SuperExternRouter）
 * @param super SuperServer 实例
 */
void SuperLoggerMsgRegister(SuperServer& super);
