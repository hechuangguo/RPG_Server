/**
 * @file    LoginGameZoneZoneMsg.cpp
 * @brief   SuperServer 区状态上报处理
 */

#include "LoginGameZoneZoneMsg.h"
#include "LoginServer.h"
#include "../sdk/log/Logger.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../protocal/InternalMsg.h"

namespace
{
void onZoneStatusReport(LoginServer& server, ConnID /*fromConn*/,
                         const Msg_Login_ZoneStatusReport& report)
{
    if (!server.zoneInfoStore().applyZoneReport(report))
    {
        LOG_WARN("忽略区状态上报: 未知区服 gameType=%u zoneId=%u",
                 report.gameType, report.zoneId);
        return;
    }
    LOG_DEBUG("区状态已更新: gameType=%u zoneId=%u online=%u gateways=%u alive=%u",
              report.gameType, report.zoneId, report.onlineCount,
              report.gatewayCount, report.alive);
}
} // namespace

void LoginGameZoneZoneMsgRegister(LoginServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalFree<LoginServer, Msg_Login_ZoneStatusReport>(
        d, server, static_cast<uint16_t>(InternalMsgID::LOGIN_ZONE_STATUS_REPORT),
        &onZoneStatusReport);
}
