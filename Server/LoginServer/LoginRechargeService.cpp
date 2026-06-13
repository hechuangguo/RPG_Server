/**
 * @file    LoginRechargeService.cpp
 */

#include "LoginRechargeService.h"
#include "LoginServer.h"
#include "../sdk/log/Logger.h"

LoginRechargeService::LoginRechargeService(LoginServer& owner)
    : m_owner(owner)
{
    (void)m_owner;
}

void LoginRechargeService::onRechargeReq(ConnID fromConn, const char* data, uint16_t len)
{
    (void)data;
    LOG_DEBUG("LoginRechargeService: skeleton req conn=%u len=%u", fromConn, len);
}
