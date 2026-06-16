/**
 * @file    LoginGmService.cpp
 */

#include "LoginGmService.h"
#include "LoginServer.h"
#include "../sdk/log/Logger.h"

LoginGmService::LoginGmService(LoginServer& owner)
    : m_owner(owner)
{
    (void)m_owner;
}

void LoginGmService::onGmCmdReq(ConnID fromConn, const char* data, uint16_t len)
{
    (void)data;
    LOG_DEBUG("GM 指令请求骨架: conn=%u len=%u", fromConn, len);
}
