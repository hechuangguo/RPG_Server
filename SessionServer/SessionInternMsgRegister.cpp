/**
 * @file    SessionInternMsgRegister.cpp
 * @brief  会话服区内 S2S 消息注册实现
 */

#include "SessionInternMsgRegister.h"
#include "SessionLoginMsg.h"
#include "SessionServer.h"
#include "../sdk/net/GwClientUnwrap.h"
#include "../sdk/net/MsgIngress.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../sdk/util/UserBase.h"
#include "../protocal/InternalMsg.h"

void SessionInternMsgRegister(SessionServer& server)
{
    auto& d = MsgDispatcher::Instance();
    d.Register(static_cast<uint16_t>(InternalMsgID::S2S_HEARTBEAT_ACK),
               [](uint32_t, const char*, uint16_t) {});

    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_RELATION_PRELOAD_RSP),
                        &SessionServer::onRelationPreloadRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_RELATION_LOAD_RSP),
                        &SessionServer::onRelationLoadRsp);
    registerInternalSized<SessionServer, UserID>(
        d, &server, static_cast<uint16_t>(InternalMsgID::SES_LOAD_USER_REQ),
        &SessionServer::onLoadUserReq);
    registerInternalSized<SessionServer, UserID>(
        d, &server, static_cast<uint16_t>(InternalMsgID::SES_SAVE_USER_REQ),
        &SessionServer::onSaveUserReq);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SES_FRIEND_UPDATE),
                        &SessionServer::onFriendUpdate);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::SES_SCENE_REGISTER_REQ),
                     &SessionServer::onSceneRegisterReq);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::SES_SCENE_UNREGISTER),
                     &SessionServer::onSceneUnregister);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::SES_COPY_CREATE_REQ),
                     &SessionServer::onCopyCreateReq);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::SES_RESOLVE_MAP_REQ),
                     &SessionServer::onResolveMapReq);

    registerGwClientUnwrapHandler(d, [&server](ConnID fromConn, const UnwrappedClientMsg& msg) {
        if (fromConn != INVALID_CONN_ID)
            server.setGatewayInboundConn(fromConn);
        MsgIngress::dispatchClient(msg.clientConnId, msg.module, msg.sub,
                                   msg.body, msg.bodyLen);
    });

    SessionLoginMsgRegister(server);
}
