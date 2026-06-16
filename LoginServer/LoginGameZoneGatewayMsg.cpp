/**
 * @file    LoginGameZoneGatewayMsg.cpp
 */

#include "LoginGameZoneGatewayMsg.h"
#include "LoginServer.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"

namespace
{
void onGatewayRegister(LoginServer& server, ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_Login_GatewayRegister))
        return;
    const auto* req = reinterpret_cast<const Msg_Login_GatewayRegister*>(data);

    LoginGatewayEntry entry;
    entry.gatewayServerId = req->gatewayServerId;
    entry.zoneId = req->zoneId;
    entry.gameType = req->gameType;
    entry.ip = req->ip;
    entry.port = req->port;
    entry.name = req->name;
    entry.zoneName = req->zoneName;
    entry.lastHeartbeatMs = TimerMgr::NowMs();
    server.gatewayRegistry().upsert(entry);

    Msg_Login_GatewayRegisterRsp rsp{};
    rsp.code = 0;
    rsp.gatewayServerId = req->gatewayServerId;
    server.registerServer().SendMsg(fromConn,
                                    static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_REGISTER_RSP),
                                    reinterpret_cast<char*>(&rsp), sizeof(rsp));
    LOG_INFO("网关注册成功: id=%u %s:%u name=%s (total=%zu)",
             req->gatewayServerId, req->ip, req->port, req->name,
             server.gatewayRegistry().size());
}

void onGatewayHeartbeat(LoginServer& server, ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_Login_GatewayRegister))
        return;
    const auto* req = reinterpret_cast<const Msg_Login_GatewayRegister*>(data);
    if (!server.gatewayRegistry().touch(req->gatewayServerId))
    {
        onGatewayRegister(server, fromConn, data, len);
        return;
    }
    LOG_DEBUG("收到网关心跳: id=%u", req->gatewayServerId);
}
} // namespace

void LoginGameZoneGatewayMsgRegister(LoginServer& server)
{
    auto& d = MsgDispatcher::Instance();
    d.Register(static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_REGISTER_REQ),
               [&server](uint32_t c, const char* data, uint16_t l) {
                   onGatewayRegister(server, c, data, l);
               });
    d.Register(static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_HEARTBEAT),
               [&server](uint32_t c, const char* data, uint16_t l) {
                   onGatewayHeartbeat(server, c, data, l);
               });
}
