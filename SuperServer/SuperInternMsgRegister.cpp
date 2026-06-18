/**
 * @file    SuperInternMsgRegister.cpp
 * @brief  超级服区内 S2S 消息注册实现
 */

#include "SuperInternMsgRegister.h"
#include "SuperServer.h"
#include "SuperExternRouter.h"
#include "SuperLoginMsg.h"
#include "SuperLoggerMsg.h"
#include "SuperGlobalMsg.h"
#include "SuperZoneMsg.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../protocal/InternalMsg.h"

void SuperInternMsgRegister(SuperServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::S2S_REGISTER_REQ),
                        &SuperServer::OnRegister);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::S2S_HEARTBEAT),
                        &SuperServer::OnHeartbeat);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::S2S_SERVERLIST_REQ),
                        &SuperServer::OnServerListReq);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::GW_USER_LOGIN_REQ),
                        &SuperServer::OnUserLoginReq);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_LOAD_USER_RSP),
                        &SuperServer::OnLoadUserRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SES_RESOLVE_MAP_RSP),
                        &SuperServer::OnResolveMapRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SCE_USER_ENTER_RSP),
                        &SuperServer::OnUserEnterRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SS_KICK_USER),
                        &SuperServer::OnKickUser);

    SuperExternMsgRegister(server);
    SuperLoginMsgRegister(server);
    SuperLoggerMsgRegister(server);
    SuperGlobalMsgRegister(server);
    SuperZoneMsgRegister(server);
}
