/**
 * @file    LoginGameZoneMsg.h
 * @brief   LoginServer 游戏区入站消息统一注册
 *
 * RegisterListen 口由 Super 连接；注册顺序：
 *   1. GameZoneMsgRegisterForwardDispatch（EXT_GAMEZONE_FWD_REQ 解包）
 *   2. LoginGameZoneGatewayMsg（LOGIN_GATEWAY_REGISTER / HEARTBEAT）
 *   3. LoginGameZoneRechargeMsg（LOGIN_RECHARGE_REQ）
 *   4. LoginGameZoneGmMsg（LOGIN_GM_CMD_REQ）
 */

#pragma once

class LoginServer;

/**
 * @brief 注册 LoginServer 全部游戏区入站 handler
 * @param server LoginServer 实例
 */
void LoginGameZoneMsgRegister(LoginServer& server);
