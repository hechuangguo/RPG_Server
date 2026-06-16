/**
 * @file    LoginGameZoneMsg.cpp
 */

#include "LoginGameZoneMsg.h"
#include "LoginGameZoneGatewayMsg.h"
#include "LoginGameZoneZoneMsg.h"
#include "LoginGameZoneRechargeMsg.h"
#include "LoginGameZoneGmMsg.h"
#include "../sdk/util/GameZoneMsgDispatch.h"

void LoginGameZoneMsgRegister(LoginServer& server)
{
    GameZoneMsgRegisterForwardDispatch();
    LoginGameZoneGatewayMsgRegister(server);
    LoginGameZoneZoneMsgRegister(server);
    LoginGameZoneRechargeMsgRegister(server);
    LoginGameZoneGmMsgRegister(server);
}
