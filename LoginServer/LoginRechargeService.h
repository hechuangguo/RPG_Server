/**
 * @file    LoginRechargeService.h
 * @brief  充值业务骨架（首期空实现）
 */

#pragma once

#include "../sdk/net/NetDefine.h"

class LoginServer;

class LoginRechargeService
{
public:
    explicit LoginRechargeService(LoginServer& owner);

    void onRechargeReq(ConnID fromConn, const char* data, uint16_t len);

private:
    LoginServer& m_owner;
};
