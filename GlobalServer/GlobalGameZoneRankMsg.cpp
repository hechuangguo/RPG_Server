/**
 * @file    GlobalGameZoneRankMsg.cpp
 */

#include "GlobalGameZoneRankMsg.h"
#include "GlobalServer.h"
#include "../protocal/InternalMsg.h"

void GlobalGameZoneRankMsgRegister(GlobalServer& server)
{
    MsgDispatcher::Instance().Register(
        static_cast<uint16_t>(InternalMsgID::GLB_RANK_UPDATE),
        [&server](uint32_t c, const char* data, uint16_t len) {
            server.onRankUpdateFromGameZone(c, data, len);
        });
}
