/**
 * @file    GlobalGameZoneSyncMsg.cpp
 */

#include "GlobalGameZoneSyncMsg.h"
#include "GlobalServer.h"
#include "../protocal/InternalMsg.h"

void GlobalGameZoneSyncMsgRegister(GlobalServer& server)
{
    MsgDispatcher::Instance().Register(
        static_cast<uint16_t>(InternalMsgID::GLB_DATA_SYNC),
        [&server](uint32_t c, const char* data, uint16_t len) {
            server.onDataSyncFromGameZone(c, data, len);
        });
}
