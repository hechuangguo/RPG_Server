/**
 * @file    LoggerGameZoneLogMsg.cpp
 */

#include "LoggerGameZoneLogMsg.h"
#include "LoggerServer.h"
#include "../sdk/util/GameZoneMsgDispatch.h"

void LoggerGameZoneMsgRegister(LoggerServer& /*server*/)
{
    GameZoneMsgRegisterForwardDispatch();
}
