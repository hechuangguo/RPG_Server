/**
 * @file    RecordInternMsgRegister.cpp
 * @brief  存档服区内 S2S 消息注册实现
 */

#include "RecordInternMsgRegister.h"
#include "RecordServer.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../protocal/InternalMsg.h"

void RecordInternMsgRegister(RecordServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_LOAD_USER_REQ),
                        &RecordServer::OnLoadUser);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_SAVE_USER_REQ),
                        &RecordServer::OnSaveUser);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_RELATION_PRELOAD_REQ),
                        &RecordServer::OnRelationPreloadReq);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_RELATION_LOAD_REQ),
                        &RecordServer::OnRelationLoadReq);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_RELATION_SAVE_REQ),
                        &RecordServer::OnRelationSaveReq);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_VALIDATE_TOKEN_REQ),
                        &RecordServer::OnValidateTokenReq);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_VERIFY_TOKEN_RSP),
                        &RecordServer::OnLoginVerifyTokenRsp);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_LIST_CHARACTERS_REQ),
                        &RecordServer::OnListCharactersReq);
    registerInternalRaw(d, &server,
                        static_cast<uint16_t>(InternalMsgID::REC_CREATE_CHARACTER_REQ),
                        &RecordServer::OnCreateCharacterReq);
}
