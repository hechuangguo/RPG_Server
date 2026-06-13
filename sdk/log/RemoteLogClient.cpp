/**
 * @file    RemoteLogClient.cpp
 * @brief   远程日志转发实现
 */

#include "RemoteLogClient.h"
#include "../util/GameZoneExternSender.h"

#include "../net/NetDefine.h"

#include <cstring>
#include <vector>

GameZoneExternSender* RemoteLogClient::s_sender     = nullptr;
SubServerType         RemoteLogClient::s_serverType = SubServerType::UNKNOWN;

void RemoteLogClient::bind(GameZoneExternSender* sender, SubServerType serverType)
{
    s_sender     = sender;
    s_serverType = serverType;
}

void RemoteLogClient::trySend(int level, const char* line, size_t lineLen)
{
    if (!s_sender || !line || lineLen == 0)
        return;
    if (lineLen > MAX_PACKET_SIZE)
        lineLen = MAX_PACKET_SIZE;

    std::vector<char> buf(sizeof(Msg_Log_WriteReq) + lineLen);
    auto* hdr = reinterpret_cast<Msg_Log_WriteReq*>(buf.data());
    hdr->serverType = static_cast<uint8_t>(s_serverType);
    hdr->level      = static_cast<uint8_t>(level);
    hdr->logLen     = static_cast<uint32_t>(lineLen);
    std::memcpy(buf.data() + sizeof(Msg_Log_WriteReq), line, lineLen);

    s_sender->sendToLoggerServer(static_cast<uint16_t>(InternalMsgID::LOG_WRITE_REQ),
                                 buf.data(), static_cast<uint16_t>(buf.size()));
}
