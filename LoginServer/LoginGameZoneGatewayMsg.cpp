/**
 * @file    LoginGameZoneGatewayMsg.cpp
 */

#include "LoginGameZoneGatewayMsg.h"
#include "LoginServer.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../protocal/InternalMsg.h"

namespace
{
void onGatewayRegister(LoginServer& server, ConnID fromConn, const Msg_Login_GatewayRegister& req)
{
    LoginGatewayEntry entry;
    entry.gatewayServerId = req.gatewayServerId;
    entry.zoneId = req.zoneId;
    entry.gameType = req.gameType;
    entry.ip = req.ip;
    entry.port = req.port;
    entry.name = req.name;
    entry.zoneName = req.zoneName;
    entry.lastHeartbeatMs = TimerMgr::NowMs();
    entry.onlineCount = req.onlineCount;
    server.gatewayRegistry().upsert(entry);

    Msg_Login_GatewayRegisterRsp rsp{};
    rsp.code = 0;
    rsp.gatewayServerId = req.gatewayServerId;
    server.registerServer().SendMsg(fromConn,
                                    static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_REGISTER_RSP),
                                    reinterpret_cast<char*>(&rsp), sizeof(rsp));
    LOG_INFO("网关注册成功: id=%u %s:%u name=%s (total=%zu)",
             req.gatewayServerId, req.ip, req.port, req.name,
             server.gatewayRegistry().size());
}

void onGatewayHeartbeat(LoginServer& server, ConnID fromConn, const Msg_Login_GatewayRegister& req)
{
    if (!server.gatewayRegistry().touch(req.gatewayServerId))
    {
        onGatewayRegister(server, fromConn, req);
        return;
    }
    LoginGatewayEntry gw;
    if (server.gatewayRegistry().pickByServerId(req.gatewayServerId, gw))
    {
        gw.onlineCount = req.onlineCount;
        gw.lastHeartbeatMs = TimerMgr::NowMs();
        server.gatewayRegistry().upsert(gw);
    }
    LOG_DEBUG("收到网关心跳: id=%u online=%u", req.gatewayServerId, req.onlineCount);
}
} // namespace

void LoginGameZoneGatewayMsgRegister(LoginServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalFree<LoginServer, Msg_Login_GatewayRegister>(
        d, server, static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_REGISTER_REQ),
        &onGatewayRegister);
    registerInternalFree<LoginServer, Msg_Login_GatewayRegister>(
        d, server, static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_HEARTBEAT),
        &onGatewayHeartbeat);
}
