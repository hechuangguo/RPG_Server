/**
 * @file    LoginAuthService.h
 * @brief  LoginServer 客户端登录（ClientListen）
 */

#pragma once

#include "../sdk/net/NetDefine.h"

class LoginServer;

class LoginAuthService
{
public:
    explicit LoginAuthService(LoginServer& owner);

    void onClientLogin(ConnID connID, const char* data, uint16_t len);

private:
    void sendGatewayInfo(ConnID connID, int32_t code, const char* msg);

    LoginServer& m_owner;
};
