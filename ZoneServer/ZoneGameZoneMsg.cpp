/**
 * @file    ZoneGameZoneMsg.cpp
 */

#include "ZoneGameZoneMsg.h"
#include "ZoneGameZoneCrossMsg.h"
#include "ZoneGameZoneForwardMsg.h"
#include "../sdk/util/GameZoneMsgDispatch.h"

void ZoneGameZoneMsgRegister(ZoneServer& server)
{
    GameZoneMsgRegisterForwardDispatch();
    ZoneGameZoneCrossMsgRegister(server);
    ZoneGameZoneForwardMsgRegister(server);
}
