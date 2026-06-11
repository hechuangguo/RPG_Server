/**
 * @file    RemoteLogClient.cpp
 * @brief   远程日志转发实现
 */

#include "RemoteLogClient.h"

#include "../net/NetDefine.h"

#include <cstring>
#include <vector>

TcpClient*    RemoteLogClient::s_client     = nullptr;
SubServerType RemoteLogClient::s_serverType = SubServerType::UNKNOWN;

void RemoteLogClient::bind(TcpClient* client, SubServerType serverType)
{
    s_client     = client;
    s_serverType = serverType;
}

void RemoteLogClient::trySend(int level, const char* line, size_t lineLen)
{
    if (!s_client || !s_client->IsConnected() || !line || lineLen == 0)
        return;
    if (lineLen > MAX_PACKET_SIZE)
        lineLen = MAX_PACKET_SIZE;

    std::vector<char> buf(sizeof(Msg_Log_WriteReq) + lineLen);
    auto* hdr = reinterpret_cast<Msg_Log_WriteReq*>(buf.data());
    hdr->serverType = (uint8_t)s_serverType;
    hdr->level      = (uint8_t)level;
    hdr->logLen     = (uint32_t)lineLen;
    std::memcpy(buf.data() + sizeof(Msg_Log_WriteReq), line, lineLen);

    s_client->SendMsg((uint16_t)InternalMsgID::LOG_WRITE_REQ,
                      buf.data(), (uint16_t)buf.size());
}
