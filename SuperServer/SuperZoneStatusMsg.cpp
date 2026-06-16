/**
 * @file    SuperZoneStatusMsg.cpp
 * @brief   SuperServer 汇总 Gateway 在线人数并上报 LoginServer
 */

#include "SuperZoneStatusMsg.h"
#include "SuperServer.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../sdk/util/ExternalServerHub.h"
#include "../protocal/InternalMsg.h"

#include <cstdint>

namespace
{
constexpr uint32_t ZONE_STATUS_REPORT_INTERVAL_MS = 15000;
} // namespace

void SuperZoneStatusMsgRegister(SuperServer& super)
{
    TimerMgr::Instance().Register(ZONE_STATUS_REPORT_INTERVAL_MS,
                                  ZONE_STATUS_REPORT_INTERVAL_MS,
                                  [&super] { super.reportZoneStatusToLogin(); });
}
