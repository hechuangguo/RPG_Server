/**
 * @file    LoginClientMsgRegister.cpp
 * @brief  登录服客户端口消息注册实现
 */

#include "LoginClientMsgRegister.h"
#include "LoginServer.h"
#include "../sdk/util/ClientMsgDispatcher.h"
#include "../Common/ClientTypes.h"
#include "../Common/LoginMsg.h"
#include "../Common/ZoneMsg.h"

void LoginClientMsgRegister(LoginServer& server)
{
    auto& d = ClientMsgDispatcher::Instance();
    d.Register(static_cast<uint8_t>(ClientModule::LOGIN),
               static_cast<uint8_t>(LoginMsgSub::C2S_LOGIN_REQ),
               [&server](uint32_t connId, const char* data, uint16_t len) {
                   server.authService().onClientLogin(static_cast<ConnID>(connId), data, len);
               });
    d.Register(static_cast<uint8_t>(ClientModule::LOGIN),
               static_cast<uint8_t>(LoginMsgSub::C2S_REGISTER_REQ),
               [&server](uint32_t connId, const char* data, uint16_t len) {
                   server.registerService().onClientRegister(static_cast<ConnID>(connId), data, len);
               });
    d.Register(static_cast<uint8_t>(ClientModule::LOGIN),
               static_cast<uint8_t>(ZoneMsgSub::C2S_ZONE_LIST_REQ),
               [&server](uint32_t connId, const char* data, uint16_t len) {
                   server.authService().onClientZoneList(static_cast<ConnID>(connId), data, len);
               });
}
