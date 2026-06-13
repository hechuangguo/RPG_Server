/**
 * @file    LoginGameZoneGmMsg.h
 * @brief   LoginServer 游戏区 GM 消息注册
 *
 * 注册 innerMsgId LOGIN_GM_CMD_REQ（0x1905），委托 LoginGmService 处理。
 * 消息经 Super EXT_GAMEZONE_FWD_REQ 解包后到达。
 */

#pragma once

class LoginServer;

/**
 * @brief 注册 GM 类 EXT 解包 handler
 * @param server LoginServer 实例
 */
void LoginGameZoneGmMsgRegister(LoginServer& server);
