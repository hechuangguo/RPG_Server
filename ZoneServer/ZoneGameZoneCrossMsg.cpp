/**
 * @file    ZoneGameZoneCrossMsg.cpp
 */

#include "ZoneGameZoneCrossMsg.h"
#include "ZoneServer.h"
#include "../protocal/InternalMsg.h"

void ZoneGameZoneCrossMsgRegister(ZoneServer& server)
{
    MsgDispatcher::Instance().Register(
        static_cast<uint16_t>(InternalMsgID::ZONE_CROSS_REQ),
        [&server](uint32_t c, const char* data, uint16_t len) {
            server.onCrossReq(c, data, len);
        });
}
