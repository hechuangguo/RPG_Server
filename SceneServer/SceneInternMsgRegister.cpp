/**
 * @file    SceneInternMsgRegister.cpp
 * @brief  场景服区内 S2S 消息注册实现
 */

#include "SceneInternMsgRegister.h"
#include "SceneLoginMsg.h"
#include "SceneServer.h"
#include "../sdk/net/GwClientUnwrap.h"
#include "../sdk/net/MsgIngress.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../sdk/util/UserBase.h"
#include "../protocal/InternalMsg.h"

void SceneInternMsgRegister(SceneServer& server)
{
    auto& d = MsgDispatcher::Instance();
    d.Register(static_cast<uint16_t>(InternalMsgID::S2S_HEARTBEAT_ACK),
               [](uint32_t, const char*, uint16_t) {});

    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::SCE_USER_ENTER_REQ),
                     &SceneServer::onUserEnter);
    registerInternalSized<SceneServer, UserID>(
        d, &server, static_cast<uint16_t>(InternalMsgID::SCE_USER_LEAVE),
        &SceneServer::onUserLeave);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::AOI_VIEW_NOTIFY),
                        &SceneServer::onViewNotify);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SES_SCENE_REGISTER_RSP),
                        &SceneServer::onSceneRegisterRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_SAVE_USER_RSP),
                        &SceneServer::onSaveUserRsp);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::SES_COPY_CREATE_RSP),
                     &SceneServer::onCopyCreateRsp);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::SES_COPY_CREATE_CMD),
                     &SceneServer::onCopyCreateCmd);

    registerGwClientUnwrapHandler(d, [](ConnID /*fromConn*/, const UnwrappedClientMsg& msg) {
        MsgIngress::dispatchClient(msg.clientConnId, msg.module, msg.sub,
                                   msg.body, msg.bodyLen);
    });

    SceneLoginMsgRegister(server);
}
