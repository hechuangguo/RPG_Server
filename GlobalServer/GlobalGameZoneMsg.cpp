/**
 * @file    GlobalGameZoneMsg.cpp
 */

#include "GlobalGameZoneMsg.h"
#include "GlobalGameZoneRankMsg.h"
#include "GlobalGameZoneSyncMsg.h"
#include "../sdk/util/GameZoneMsgDispatch.h"

void GlobalGameZoneMsgRegister(GlobalServer& server)
{
    GameZoneMsgRegisterForwardDispatch();
    GlobalGameZoneRankMsgRegister(server);
    GlobalGameZoneSyncMsgRegister(server);
}
