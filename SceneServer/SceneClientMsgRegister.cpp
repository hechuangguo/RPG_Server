/**
 * @file    SceneClientMsgRegister.cpp
 * @brief  场景服客户端消息注册实现
 */

#include "SceneClientMsgRegister.h"
#include "SceneServer.h"
#include "../sdk/util/ClientMsgDispatcher.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../Common/ChatMsg.h"
#include "../Common/MapDataMsg.h"

void SceneClientMsgRegister(SceneServer& server)
{
    auto& d = ClientMsgDispatcher::Instance();
    registerClientRawU32(d, &server, Msg_C2S_MoveReq::kModule,
                         static_cast<uint8_t>(SceneMsgSub::C2S_MOVE_REQ),
                         &SceneServer::onMoveReq);
    registerClientRawU32(d, &server, Msg_C2S_Chat::kModule,
                         static_cast<uint8_t>(ChatMsgSub::C2S_CHAT_REQ),
                         &SceneServer::onChatReq);
    registerClientRawU32(d, &server, Msg_C2S_NpcTalkReq::kModule,
                         static_cast<uint8_t>(NpcMsgSub::C2S_NPC_TALK_REQ),
                         &SceneServer::onNpcTalkReq);
}
