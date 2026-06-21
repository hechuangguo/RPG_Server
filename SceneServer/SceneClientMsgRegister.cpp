/**
 * @file    SceneClientMsgRegister.cpp
 * @brief  场景服客户端消息注册实现
 */

#include "SceneClientMsgRegister.h"
#include "SceneServer.h"
#include "../sdk/util/ClientMsgDispatcher.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "ClientCommon.pb.h"
#include "MapDataCommon.pb.h"
#include "ChatCommon.pb.h"
#include "NpcCommon.pb.h"

void SceneClientMsgRegister(SceneServer& server)
{
    auto& d = ClientMsgDispatcher::Instance();
    registerClientRawU32(d, &server, static_cast<uint8_t>(rpg::client::SCENE),
                         static_cast<uint8_t>(rpg::mapdata::C2S_MOVE_REQ),
                         &SceneServer::onMoveReq);
    registerClientRawU32(d, &server, static_cast<uint8_t>(rpg::client::CHAT),
                         static_cast<uint8_t>(rpg::chat::C2S_CHAT_REQ),
                         &SceneServer::onChatReq);
    registerClientRawU32(d, &server, static_cast<uint8_t>(rpg::client::NPC),
                         static_cast<uint8_t>(rpg::npc::C2S_NPC_TALK_REQ),
                         &SceneServer::onNpcTalkReq);
}
