/**
 * @file    LoginGmService.h
 * @brief  GM 业务骨架（首期空实现）
 */

#pragma once

#include "../sdk/net/NetDefine.h"

class LoginServer;

class LoginGmService
{
public:
    explicit LoginGmService(LoginServer& owner);

    void onGmCmdReq(ConnID fromConn, const char* data, uint16_t len);

private:
    LoginServer& m_owner;
};
