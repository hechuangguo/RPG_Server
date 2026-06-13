/**
 * @file    GatewayUser.cpp
 * @brief  GatewayUser 下行与日志封装
 */

#include "GatewayUser.h"
#include "GatewayServer.h"
#include "../sdk/log/UserLog.h"

#include <cstdarg>

bool GatewayUser::sendCmdToMe(uint8_t module, uint8_t sub, const char* data, uint16_t len)
{
    return GatewayServer::Instance()->sendToClient(getConnId(), module, sub, data, len);
}

bool GatewayUser::sendCmdToMe(uint16_t flatMsgId, const char* data, uint16_t len)
{
    return sendCmdToMe(static_cast<uint8_t>(flatMsgId >> 8),
                       static_cast<uint8_t>(flatMsgId & 0xFF),
                       data, len);
}

void GatewayUser::info(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UserLog::info(*this, "GatewayUser", "%s", buf);
}

void GatewayUser::debug(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UserLog::debug(*this, "GatewayUser", "%s", buf);
}

void GatewayUser::warn(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UserLog::warn(*this, "GatewayUser", "%s", buf);
}

void GatewayUser::error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UserLog::error(*this, "GatewayUser", "%s", buf);
}
