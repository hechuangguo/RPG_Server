/**
 * @file    LoginGameZoneMsg.cpp
 */

#include "LoginGameZoneMsg.h"
#include "LoginGameZoneGatewayMsg.h"
#include "LoginGameZoneRechargeMsg.h"
#include "LoginGameZoneGmMsg.h"
#include "../sdk/util/GameZoneMsgDispatch.h"

void LoginGameZoneMsgRegister(LoginServer& server)
{
    GameZoneMsgRegisterForwardDispatch();
    LoginGameZoneGatewayMsgRegister(server);
    LoginGameZoneRechargeMsgRegister(server);
    LoginGameZoneGmMsgRegister(server);
}
