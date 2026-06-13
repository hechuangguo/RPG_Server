/**
 * @file    SessionLoginMsg.h
 * @brief   SessionServer 处理经 Super 转发的 Login 区服指令
 *
 * 注册：
 *   - SS_EXTERN_FWD_RSP（0x1F11）：Login/外联回包到 Session（按 innerMsgId/seq 占位）
 *   - LOGIN_RECHARGE_REQ（0x1904）：Login 经 Super 下发的充值骨架（Session 侧重社交/订单）
 */

#pragma once

class SessionServer;

/**
 * @brief 注册 SessionServer Login 相关区内 handler
 * @param server SessionServer 实例
 */
void SessionLoginMsgRegister(SessionServer& server);
