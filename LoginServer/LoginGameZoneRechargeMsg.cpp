/**
 * @file    LoginGameZoneRechargeMsg.cpp
 */

#include "LoginGameZoneRechargeMsg.h"
#include "LoginServer.h"
#include "LoginRechargeService.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../protocal/InternalMsg.h"

void LoginGameZoneRechargeMsgRegister(LoginServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalRaw(d, &server.rechargeService(),
                        static_cast<uint16_t>(InternalMsgID::LOGIN_RECHARGE_REQ),
                        &LoginRechargeService::onRechargeReq);
}
