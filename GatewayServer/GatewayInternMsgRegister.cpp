/**
 * @file    GatewayInternMsgRegister.cpp
 * @brief  网关服区内 S2S 消息注册实现
 */

#include "GatewayInternMsgRegister.h"
#include "GatewayServer.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../protocal/InternalMsg.h"

void GatewayInternMsgRegister(GatewayServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::GW_SEND_TO_CLIENT),
                        &GatewayServer::OnSendToClient);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::GW_KICK_CLIENT),
                        &GatewayServer::OnKickClient);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_VALIDATE_TOKEN_RSP),
                        &GatewayServer::OnValidateTokenRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_LIST_CHARACTERS_RSP),
                        &GatewayServer::OnListCharactersRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_CREATE_CHARACTER_RSP),
                        &GatewayServer::OnCreateCharacterRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::GW_USER_LOGIN_RSP),
                        &GatewayServer::OnUserLoginRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::S2S_REGISTER_RSP),
                        &GatewayServer::onSuperRegisterRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SS_LOGIN_GATEWAY_WRAP_RSP),
                        &GatewayServer::onLoginGatewayWrapRsp);
}
