/**
 * @file    LoginGameZoneGmMsg.cpp
 */

#include "LoginGameZoneGmMsg.h"
#include "LoginServer.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../protocal/InternalMsg.h"

void LoginGameZoneGmMsgRegister(LoginServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalRaw(d, &server.gmService(),
                        static_cast<uint16_t>(InternalMsgID::LOGIN_GM_CMD_REQ),
                        &LoginGmService::onGmCmdReq);
}
