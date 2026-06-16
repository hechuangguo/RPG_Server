/**
 * @file    LoginGameZoneZoneMsg.cpp
 * @brief   SuperServer 区状态上报处理
 */

#include "LoginGameZoneZoneMsg.h"
#include "LoginServer.h"
#include "../sdk/log/Logger.h"
#include "../protocal/InternalMsg.h"

namespace
{
void onZoneStatusReport(LoginServer& server, ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_Login_ZoneStatusReport))
        return;
    const auto* report = reinterpret_cast<const Msg_Login_ZoneStatusReport*>(data);
    if (!server.zoneInfoStore().applyZoneReport(*report))
    {
        LOG_WARN("忽略区状态上报: 未知区服 gameType=%u zoneId=%u",
                 report->gameType, report->zoneId);
        return;
    }
    LOG_DEBUG("区状态已更新: gameType=%u zoneId=%u online=%u gateways=%u alive=%u",
              report->gameType, report->zoneId, report->onlineCount,
              report->gatewayCount, report->alive);
}
} // namespace

void LoginGameZoneZoneMsgRegister(LoginServer& server)
{
    auto& d = MsgDispatcher::Instance();
    d.Register(static_cast<uint16_t>(InternalMsgID::LOGIN_ZONE_STATUS_REPORT),
               [&server](uint32_t c, const char* data, uint16_t l) {
                   onZoneStatusReport(server, c, data, l);
               });
}
