/**
 * @file    AoiInternMsgRegister.cpp
 * @brief  视野服区内 S2S 消息注册实现
 */

#include "AoiInternMsgRegister.h"
#include "AOIServer.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../protocal/InternalMsg.h"

void AoiInternMsgRegister(AOIServer& server)
{
    auto& d = MsgDispatcher::Instance();
    d.Register(static_cast<uint16_t>(InternalMsgID::S2S_HEARTBEAT_ACK),
               [](uint32_t, const char*, uint16_t) {});

    registerInternalSized<AOIServer, Msg_AOI_Move>(
        d, &server, static_cast<uint16_t>(InternalMsgID::AOI_ENTER_REQ), &AOIServer::onEnter);
    registerInternalSized<AOIServer, uint64_t>(
        d, &server, static_cast<uint16_t>(InternalMsgID::AOI_LEAVE_REQ), &AOIServer::onLeave);
    registerInternalSized<AOIServer, Msg_AOI_Move>(
        d, &server, static_cast<uint16_t>(InternalMsgID::AOI_MOVE_REQ), &AOIServer::onMove);
    registerInternalSized<AOIServer, Msg_AOI_SceneRegister>(
        d, &server, static_cast<uint16_t>(InternalMsgID::AOI_SCENE_REGISTER),
        &AOIServer::onSceneRegister);
    registerInternalSized<AOIServer, Msg_AOI_SceneUnregister>(
        d, &server, static_cast<uint16_t>(InternalMsgID::AOI_SCENE_UNREGISTER),
        &AOIServer::onSceneUnregister);
}
