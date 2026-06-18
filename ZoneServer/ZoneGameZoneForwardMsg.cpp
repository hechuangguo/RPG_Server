/**
 * @file    ZoneGameZoneForwardMsg.cpp
 */

#include "ZoneGameZoneForwardMsg.h"
#include "ZoneServer.h"
#include "../protocal/InternalMsg.h"

void ZoneGameZoneForwardMsgRegister(ZoneServer& server)
{
    MsgDispatcher::Instance().Register(
        static_cast<uint16_t>(InternalMsgID::ZONE_FORWARD),
        [&server](uint32_t c, const char* data, uint16_t len) {
            server.onForward(c, data, len);
        });
}
