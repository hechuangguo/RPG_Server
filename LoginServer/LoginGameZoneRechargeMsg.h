/**
 * @file    LoginGameZoneRechargeMsg.h
 * @brief   LoginServer 游戏区充值消息注册
 *
 * 注册 innerMsgId LOGIN_RECHARGE_REQ（0x1904），委托 LoginRechargeService 处理。
 * 消息经 Super EXT_GAMEZONE_FWD_REQ 解包后到达。
 */

#pragma once

class LoginServer;

/**
 * @brief 注册充值类 EXT 解包 handler
 * @param server LoginServer 实例
 */
void LoginGameZoneRechargeMsgRegister(LoginServer& server);
