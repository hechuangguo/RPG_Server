/**
 * @file    GatewayClientMsgRegister.cpp
 * @brief  网关服客户端 LOCAL 消息注册实现
 */

#include "GatewayClientMsgRegister.h"
#include "GatewayServer.h"
#include "../sdk/util/ClientMsgDispatcher.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../Common/LoginMsg.h"

void GatewayClientMsgRegister(GatewayServer& server)
{
    auto& d = ClientMsgDispatcher::Instance();
    registerClientRaw(d, &server, Msg_C2S_GatewayAuthReq::kModule,
                      static_cast<uint8_t>(LoginMsgSub::C2S_GATEWAY_AUTH_REQ),
                      &GatewayServer::onGatewayAuth);
    registerClientRaw(d, &server, Msg_C2S_SelectUserReq::kModule,
                      static_cast<uint8_t>(LoginMsgSub::C2S_SELECT_USER_REQ),
                      &GatewayServer::onSelectUser);
    registerClientRaw(d, &server, Msg_C2S_CreateUserReq::kModule,
                      static_cast<uint8_t>(LoginMsgSub::C2S_CREATE_USER_REQ),
                      &GatewayServer::onCreateUser);
    registerClientRaw(d, &server, Msg_C2S_Heartbeat::kModule,
                      static_cast<uint8_t>(SystemMsgSub::C2S_HEARTBEAT),
                      &GatewayServer::onClientHeartbeat);
}
