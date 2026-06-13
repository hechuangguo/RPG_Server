/**
 * @file    LoginGameZoneRechargeMsg.cpp
 */

#include "LoginGameZoneRechargeMsg.h"
#include "LoginServer.h"
#include "LoginRechargeService.h"
#include "../protocal/InternalMsg.h"

void LoginGameZoneRechargeMsgRegister(LoginServer& server)
{
    auto& d = MsgDispatcher::Instance();
    d.Register(static_cast<uint16_t>(InternalMsgID::LOGIN_RECHARGE_REQ),
               [&server](uint32_t c, const char* data, uint16_t l) {
                   server.rechargeService().onRechargeReq(c, data, l);
               });
}
