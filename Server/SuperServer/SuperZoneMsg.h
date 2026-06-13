/**
 * @file    SuperZoneMsg.h
 * @brief   SuperServer Zone 外联消息注册
 *
 * Zone 业务（ZONE_CROSS_REQ、ZONE_FORWARD 等）由 SuperExternRouter 经 SS_EXTERN_FWD 统一转发。
 * 本模块为 Super 侧 Zone 专用 handler 注册占位，便于后续扩展。
 */

#pragma once

class SuperServer;

/**
 * @brief 注册 Zone 相关 Super 侧 handler（当前为占位，主路径见 SuperExternRouter）
 * @param super SuperServer 实例
 */
void SuperZoneMsgRegister(SuperServer& super);
