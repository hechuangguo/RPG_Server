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
#include "../sdk/util/UserBase.h"
#include "../protocal/InternalMsg.h"

void SuperInternMsgRegister(SuperServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::S2S_REGISTER_REQ),
                     &SuperServer::onRegister);
    registerInternalSized<SuperServer, Msg_S2S_Heartbeat>(
        d, &server, static_cast<uint16_t>(InternalMsgID::S2S_HEARTBEAT),
        &SuperServer::onHeartbeat);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::S2S_SERVERLIST_REQ),
                        &SuperServer::onServerListReq);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::GW_USER_LOGIN_REQ),
                     &SuperServer::onUserLoginReq);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_LOAD_USER_RSP),
                        &SuperServer::onLoadUserRsp);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::SES_RESOLVE_MAP_RSP),
                     &SuperServer::onResolveMapRsp);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::SCE_USER_ENTER_RSP),
                     &SuperServer::onUserEnterRsp);
    registerInternalSized<SuperServer, UserID>(
        d, &server, static_cast<uint16_t>(InternalMsgID::SS_KICK_USER),
        &SuperServer::onKickUser);

    SuperExternMsgRegister(server);
    SuperLoginMsgRegister(server);
    SuperLoggerMsgRegister(server);
    SuperGlobalMsgRegister(server);
    SuperZoneMsgRegister(server);
}
