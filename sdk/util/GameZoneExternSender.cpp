/**
 * @file    GameZoneExternSender.cpp
 * @brief  GameZoneExternSender 实现
 */

#include "GameZoneExternSender.h"

#include <cstring>
#include <vector>

GameZoneExternSender::GameZoneExternSender(TcpClient& superClient,
                                           SubServerType selfType, uint32_t selfId)
    : m_superClient(superClient)
    , m_selfType(selfType)
    , m_selfId(selfId)
{
}

bool GameZoneExternSender::sendForward(SubServerType target, uint16_t innerMsgId,
                                       const char* data, uint16_t len, uint32_t seq)
{
    if (!m_superClient.IsConnected())
        return false;

    std::vector<char> buf(sizeof(Msg_SS_ExternForward) + len);
    auto* hdr = reinterpret_cast<Msg_SS_ExternForward*>(buf.data());
    hdr->sourceServerType = static_cast<uint8_t>(m_selfType);
    hdr->sourceServerId   = m_selfId;
    hdr->targetServerType = static_cast<uint8_t>(target);
    hdr->innerMsgId       = innerMsgId;
    hdr->seq              = seq;
    hdr->dataLen          = len;
    if (len > 0 && data)
        std::memcpy(buf.data() + sizeof(Msg_SS_ExternForward), data, len);

    if (!m_superClient.SendMsg(static_cast<uint16_t>(InternalMsgID::SS_EXTERN_FWD_REQ),
                               buf.data(), static_cast<uint16_t>(buf.size())))
        return false;
    return true;
}

bool GameZoneExternSender::sendToLoginServer(uint16_t innerMsgId, const char* data,
                                             uint16_t len, uint32_t seq)
{
    return sendForward(SubServerType::LOGIN, innerMsgId, data, len, seq);
}

bool GameZoneExternSender::sendToLoggerServer(uint16_t innerMsgId, const char* data, uint16_t len)
{
    return sendForward(SubServerType::LOGGER, innerMsgId, data, len, 0);
}

bool GameZoneExternSender::sendToGlobalServer(uint16_t innerMsgId, const char* data,
                                              uint16_t len, uint32_t seq)
{
    return sendForward(SubServerType::GLOBAL, innerMsgId, data, len, seq);
}

bool GameZoneExternSender::sendToZoneServer(uint16_t innerMsgId, const char* data,
                                            uint16_t len, uint32_t seq)
{
    return sendForward(SubServerType::ZONE, innerMsgId, data, len, seq);
}
