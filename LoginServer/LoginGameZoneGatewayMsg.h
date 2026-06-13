/**
 * @file    LoginGameZoneGatewayMsg.h
 * @brief  LoginServer 游戏区网关注册/心跳（Super → RegisterListen）
 */

#pragma once

class LoginServer;

void LoginGameZoneGatewayMsgRegister(LoginServer& server);
