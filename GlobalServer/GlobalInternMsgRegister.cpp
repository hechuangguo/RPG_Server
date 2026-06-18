/**
 * @file    GlobalInternMsgRegister.cpp
 * @brief   全局服区内 S2S 消息注册实现
 */

#include "GlobalInternMsgRegister.h"
#include "GlobalServer.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../sdk/util/GameZoneMsgDispatch.h"
#include "../protocal/InternalMsg.h"

void GlobalInternMsgRegister(GlobalServer& server)
{
    GameZoneMsgRegisterForwardDispatch();
    auto& d = MsgDispatcher::Instance();
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::GLB_RANK_UPDATE),
                        &GlobalServer::onRankUpdate);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::GLB_DATA_SYNC),
                        &GlobalServer::onDataSync);
}
