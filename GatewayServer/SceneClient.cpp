/**
 * @file    SceneClient.cpp
 * @brief   SceneClient 实现
 */

#include "SceneClient.h"

#include "../sdk/log/Logger.h"
#include "../sdk/net/GwClientRelay.h"
#include "../sdk/net/NetTls.h"

#include <cstring>

SceneClient::SceneClient(uint32_t sceneServerId, INetCallback* cb)
    : m_sceneServerId(sceneServerId)
    , m_client(std::make_unique<TcpClient>(cb))
{
}

bool SceneClient::connect(const std::string& ip, uint16_t port)
{
    wireTlsClient(*m_client);
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

    return sendGwClientMsg(*m_client, clientConnId, module, sub, data, len);
}

bool SceneClient::sendMsg(uint16_t msgId, const char* data, uint16_t len)
{
    if (!isConnected())
        return false;
    return m_client->SendMsg(msgId, data, len);
}
