/**
 * @file    GatewayClientMsgRegister.cpp
 * @brief  网关服客户端 LOCAL 消息注册实现
 */

#include "GatewayClientMsgRegister.h"
#include "GatewayServer.h"
#include "../Common/ClientTypes.h"
#include "../sdk/util/ClientMsgDispatcher.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "LoginCommon.pb.h"
#include "SystemCommon.pb.h"

void GatewayClientMsgRegister(GatewayServer& server)
{
    auto& d = ClientMsgDispatcher::Instance();
    const uint8_t loginMod = static_cast<uint8_t>(ClientModule::LOGIN);
    const uint8_t sysMod = static_cast<uint8_t>(ClientModule::SYSTEM);

    registerClientRaw(d, &server, loginMod,
                      static_cast<uint8_t>(rpg::login::C2S_GATEWAY_AUTH_REQ),
                      &GatewayServer::onGatewayAuth);
    registerClientRaw(d, &server, loginMod,
                      static_cast<uint8_t>(rpg::login::C2S_SELECT_USER_REQ),
                      &GatewayServer::onSelectUser);
    registerClientRaw(d, &server, loginMod,
                      static_cast<uint8_t>(rpg::login::C2S_CREATE_USER_REQ),
                      &GatewayServer::onCreateUser);
    registerClientRaw(d, &server, loginMod,
                      static_cast<uint8_t>(rpg::login::C2S_LOGOUT_REQ),
                      &GatewayServer::onLogoutReq);
    registerClientRaw(d, &server, sysMod,
                      static_cast<uint8_t>(rpg::system::C2S_HEARTBEAT),
                      &GatewayServer::onClientHeartbeat);
}
