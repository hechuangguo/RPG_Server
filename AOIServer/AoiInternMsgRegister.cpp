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
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::AOI_ENTER_REQ),
                        &AOIServer::OnEnter);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::AOI_LEAVE_REQ),
                        &AOIServer::OnLeave);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::AOI_MOVE_REQ),
                        &AOIServer::OnMove);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::AOI_SCENE_REGISTER),
                        &AOIServer::OnSceneRegister);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::AOI_SCENE_UNREGISTER),
                        &AOIServer::OnSceneUnregister);
}
