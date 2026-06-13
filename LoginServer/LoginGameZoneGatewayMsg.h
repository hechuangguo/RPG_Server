/**
 * @file    LoginGameZoneGatewayMsg.h
 * @brief   LoginServer 游戏区网关注册/心跳（Super → RegisterListen）
 *
 * 处理 Super 转发的原生协议（不经 EXT 信封）：
 *   - LOGIN_GATEWAY_REGISTER_REQ（0x1901）：写入 LoginGatewayRegistry
 *   - LOGIN_GATEWAY_HEARTBEAT（0x1903）：刷新或补注册
 */

#pragma once

class LoginServer;

/**
 * @brief 注册网关注册与心跳 handler
 * @param server LoginServer 实例
 */
void LoginGameZoneGatewayMsgRegister(LoginServer& server);
