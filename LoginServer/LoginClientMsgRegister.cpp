/**
 * @file    LoginClientMsgRegister.cpp
 * @brief  登录服客户端口消息注册实现
 */

#include "LoginClientMsgRegister.h"
#include "LoginServer.h"
#include "LoginAuthService.h"
#include "LoginRegisterService.h"
#include "../sdk/util/ClientMsgDispatcher.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../Common/ClientTypes.h"
#include "../Common/LoginMsg.h"
#include "../Common/ZoneMsg.h"

void LoginClientMsgRegister(LoginServer& server)
{
    auto& d = ClientMsgDispatcher::Instance();
    registerClientServiceRaw(d, &server.authService(),
                             static_cast<uint8_t>(ClientModule::LOGIN),
                             static_cast<uint8_t>(LoginMsgSub::C2S_LOGIN_REQ),
                             &LoginAuthService::onClientLogin);
    registerClientServiceRaw(d, &server.registerService(),
                             static_cast<uint8_t>(ClientModule::LOGIN),
                             static_cast<uint8_t>(LoginMsgSub::C2S_REGISTER_REQ),
                             &LoginRegisterService::onClientRegister);
    registerClientServiceRaw(d, &server.authService(),
                             static_cast<uint8_t>(ClientModule::LOGIN),
                             static_cast<uint8_t>(ZoneMsgSub::C2S_ZONE_LIST_REQ),
                             &LoginAuthService::onClientZoneList);
}
