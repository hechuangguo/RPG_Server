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
                        &GatewayServer::onSendToClient);
    registerInternalSized<GatewayServer, uint32_t>(
        d, &server, static_cast<uint16_t>(InternalMsgID::GW_KICK_CLIENT),
        &GatewayServer::onKickClient);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::REC_VALIDATE_TOKEN_RSP),
                     &GatewayServer::onValidateTokenRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_LIST_CHARACTERS_RSP),
                        &GatewayServer::onListCharactersRsp);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::REC_CREATE_CHARACTER_RSP),
                     &GatewayServer::onCreateCharacterRsp);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::GW_USER_LOGIN_RSP),
                     &GatewayServer::onUserLoginRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::S2S_REGISTER_RSP),
                        &GatewayServer::onSuperRegisterRsp);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::SS_LOGIN_GATEWAY_WRAP_RSP),
                     &GatewayServer::onLoginGatewayWrapRsp);
}
