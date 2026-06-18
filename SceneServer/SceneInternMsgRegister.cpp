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
#include "../protocal/InternalMsg.h"

void SceneInternMsgRegister(SceneServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SCE_USER_ENTER_REQ),
                        &SceneServer::OnUserEnter);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SCE_USER_LEAVE),
                        &SceneServer::OnUserLeave);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::AOI_VIEW_NOTIFY),
                        &SceneServer::OnViewNotify);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SES_SCENE_REGISTER_RSP),
                        &SceneServer::OnSceneRegisterRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_SAVE_USER_RSP),
                        &SceneServer::OnSaveUserRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SES_COPY_CREATE_RSP),
                        &SceneServer::OnCopyCreateRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SES_COPY_CREATE_CMD),
                        &SceneServer::OnCopyCreateCmd);

    registerGwClientUnwrapHandler(d, [](ConnID /*fromConn*/, const UnwrappedClientMsg& msg) {
        MsgIngress::dispatchClient(msg.clientConnId, msg.module, msg.sub,
                                   msg.body, msg.bodyLen);
    });

    SceneLoginMsgRegister(server);
}
