/**
 * @file    SceneClient.cpp
 * @brief   SceneClient 实现
 */

#include "SceneClient.h"

#include "../sdk/log/Logger.h"

#include <cstring>
#include <vector>

SceneClient::SceneClient(uint32_t sceneServerId, INetCallback* cb)
    : m_sceneServerId(sceneServerId)
    , m_client(std::make_unique<TcpClient>(cb))
{
}

bool SceneClient::connect(const std::string& ip, uint16_t port)
{
    if (!m_client->Connect(ip, port))
    {
        LOG_WARN("场景连接: 连接失败 sceneId=%u %s:%u",
                 m_sceneServerId, ip.c_str(), port);
        return false;
    }
    LOG_INFO("场景连接: 发起连接 sceneId=%u %s:%u", m_sceneServerId, ip.c_str(), port);
    return true;
}

void SceneClient::poll()
{
    if (m_client)
        m_client->Poll(0);
}

bool SceneClient::isConnected() const
{
    return m_client && m_client->IsConnected();
}

bool SceneClient::forwardClientMsg(uint32_t clientConnId, uint8_t module, uint8_t sub,
                                   const char* data, uint16_t len)
{
    if (!isConnected())
    {
        LOG_WARN("场景连接: 转发跳过 sceneId=%u（未连接）", m_sceneServerId);
        return false;
    }

    std::vector<char> buf(sizeof(Msg_GW_ClientMsg) + len);
    auto* hdr = reinterpret_cast<Msg_GW_ClientMsg*>(buf.data());
    hdr->clientConnID = clientConnId;
    hdr->module = module;
    hdr->sub = sub;
    hdr->dataLen = len;
    if (len > 0)
        memcpy(buf.data() + sizeof(Msg_GW_ClientMsg), data, len);

    return m_client->SendMsg(static_cast<uint16_t>(InternalMsgID::GW_CLIENT_MSG),
                             buf.data(), static_cast<uint16_t>(buf.size()));
}

bool SceneClient::sendMsg(uint16_t msgId, const char* data, uint16_t len)
{
    if (!isConnected())
        return false;
    return m_client->SendMsg(msgId, data, len);
}
