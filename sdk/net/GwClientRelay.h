/**
 * @file    GwClientRelay.h
 * @brief  Gateway 与 Scene/Session 间客户端消息中继封装（GW_CLIENT_MSG / GW_SEND_TO_CLIENT）
 */

#pragma once

#include "TcpClient.h"
#include "TcpServer.h"
#include "../../protocal/InternalMsg.h"

#include <cstring>
#include <vector>

/**
 * @brief 打包 GW_CLIENT_MSG（Gateway → Scene/Session）
 * @return 完整服间消息体（含 Msg_GW_ClientMsg 头 + 客户端 body）
 */
inline std::vector<char> packGwClientMsg(uint32_t clientConnId, uint8_t module, uint8_t sub,
                                         const char* body, uint16_t bodyLen)
{
    std::vector<char> buf(sizeof(Msg_GW_ClientMsg) + bodyLen);
    auto* hdr = reinterpret_cast<Msg_GW_ClientMsg*>(buf.data());
    hdr->clientConnID = clientConnId;
    hdr->module       = module;
    hdr->sub          = sub;
    hdr->dataLen      = bodyLen;
    if (bodyLen > 0 && body)
        std::memcpy(buf.data() + sizeof(Msg_GW_ClientMsg), body, bodyLen);
    return buf;
}

/**
 * @brief 打包 GW_SEND_TO_CLIENT（Scene/Session → Gateway → Client）
 */
inline std::vector<char> packGwSendToClient(uint32_t clientConnId, uint8_t module, uint8_t sub,
                                            const char* body, uint16_t bodyLen)
{
    std::vector<char> buf(sizeof(Msg_GW_SendToClient) + bodyLen);
    auto* hdr = reinterpret_cast<Msg_GW_SendToClient*>(buf.data());
    hdr->clientConnID = clientConnId;
    hdr->module       = module;
    hdr->sub          = sub;
    hdr->dataLen      = bodyLen;
    if (bodyLen > 0 && body)
        std::memcpy(buf.data() + sizeof(Msg_GW_SendToClient), body, bodyLen);
    return buf;
}

/** @brief 经 TcpClient 发送 GW_CLIENT_MSG */
inline bool sendGwClientMsg(TcpClient& client, uint32_t clientConnId,
                            uint8_t module, uint8_t sub,
                            const char* body, uint16_t bodyLen)
{
    auto buf = packGwClientMsg(clientConnId, module, sub, body, bodyLen);
    return client.SendMsg(static_cast<uint16_t>(InternalMsgID::GW_CLIENT_MSG),
                          buf.data(), static_cast<uint16_t>(buf.size()));
}

/** @brief 经 TcpServer 向 Gateway 入站连接发送 GW_SEND_TO_CLIENT */
inline bool sendGwSendToClient(TcpServer& server, ConnID gatewayConn,
                               uint32_t clientConnId, uint8_t module, uint8_t sub,
                               const char* body, uint16_t bodyLen)
{
    auto buf = packGwSendToClient(clientConnId, module, sub, body, bodyLen);
    return server.SendMsg(gatewayConn,
                          static_cast<uint16_t>(InternalMsgID::GW_SEND_TO_CLIENT),
                          buf.data(), static_cast<uint16_t>(buf.size()));
}
