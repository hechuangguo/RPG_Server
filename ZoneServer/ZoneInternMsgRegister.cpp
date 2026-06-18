/**
 * @file    ZoneInternMsgRegister.cpp
 * @brief   跨区服区内 S2S 消息注册实现
 */

#include "ZoneInternMsgRegister.h"
#include "ZoneServer.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../sdk/util/GameZoneMsgDispatch.h"
#include "../protocal/InternalMsg.h"

void ZoneInternMsgRegister(ZoneServer& server)
{
    GameZoneMsgRegisterForwardDispatch();
    auto& d = MsgDispatcher::Instance();
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::ZONE_FORWARD),
                        &ZoneServer::onForward);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::ZONE_CROSS_REQ),
                        &ZoneServer::onCrossReq);
}
