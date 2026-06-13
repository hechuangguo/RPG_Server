/**
 * @file    SuperGlobalMsg.h
 * @brief   SuperServer Global 外联消息注册
 *
 * Global 业务（GLB_RANK_UPDATE、GLB_DATA_SYNC 等）由 SuperExternRouter 经 SS_EXTERN_FWD 统一转发。
 * 本模块为 Super 侧 Global 专用 handler 注册占位，便于后续扩展。
 */

#pragma once

class SuperServer;

/**
 * @brief 注册 Global 相关 Super 侧 handler（当前为占位，主路径见 SuperExternRouter）
 * @param super SuperServer 实例
 */
void SuperGlobalMsgRegister(SuperServer& super);
