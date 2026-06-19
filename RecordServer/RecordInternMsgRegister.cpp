/**
 * @file    RecordInternMsgRegister.cpp
 * @brief  存档服区内 S2S 消息注册实现
 */

#include "RecordInternMsgRegister.h"
#include "RecordServer.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../sdk/util/UserBase.h"
#include "../protocal/InternalMsg.h"

void RecordInternMsgRegister(RecordServer& server)
{
    auto& d = MsgDispatcher::Instance();
    d.Register(static_cast<uint16_t>(InternalMsgID::S2S_HEARTBEAT_ACK),
               [](uint32_t, const char*, uint16_t) {});

    registerInternalSized<RecordServer, UserID>(
        d, &server, static_cast<uint16_t>(InternalMsgID::REC_LOAD_USER_REQ),
        &RecordServer::onLoadUser);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_SAVE_USER_REQ),
                        &RecordServer::onSaveUser);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_RELATION_PRELOAD_REQ),
                        &RecordServer::onRelationPreloadReq);
    registerInternalSized<RecordServer, UserID>(
        d, &server, static_cast<uint16_t>(InternalMsgID::REC_RELATION_LOAD_REQ),
        &RecordServer::onRelationLoadReq);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_RELATION_SAVE_REQ),
                        &RecordServer::onRelationSaveReq);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::REC_VALIDATE_TOKEN_REQ),
                     &RecordServer::onValidateTokenReq);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::SS_EXTERN_FWD_RSP),
                        &RecordServer::onExternForwardRsp);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::REC_VERIFY_TOKEN_RSP),
                     &RecordServer::onLoginVerifyTokenRsp);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::REC_LIST_CHARACTERS_REQ),
                     &RecordServer::onListCharactersReq);
    registerInternal(d, &server,
                     static_cast<uint16_t>(InternalMsgID::REC_CREATE_CHARACTER_REQ),
                     &RecordServer::onCreateCharacterReq);
}
