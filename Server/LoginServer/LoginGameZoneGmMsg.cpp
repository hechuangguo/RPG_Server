/**
 * @file    LoginGameZoneGmMsg.cpp
 */

#include "LoginGameZoneGmMsg.h"
#include "LoginServer.h"
#include "../protocal/InternalMsg.h"

void LoginGameZoneGmMsgRegister(LoginServer& server)
{
    auto& d = MsgDispatcher::Instance();
    d.Register(static_cast<uint16_t>(InternalMsgID::LOGIN_GM_CMD_REQ),
               [&server](uint32_t c, const char* data, uint16_t l) {
                   server.gmService().onGmCmdReq(c, data, l);
               });
}
