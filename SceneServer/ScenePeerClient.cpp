/**
 * @file    ScenePeerClient.cpp
 * @brief   ScenePeerClient 实现
 */

#include "ScenePeerClient.h"

ScenePeerClient::ScenePeerClient(const char* peerName)
    : m_client(&m_callback)
    , m_peerName(peerName)
{
}

bool ScenePeerClient::connect(const std::string& ip, uint16_t port)
{
    return m_client.Connect(ip, port);
}

void ScenePeerClient::poll()
{
    m_client.Poll(0);
}

bool ScenePeerClient::isConnected() const
{
    return m_client.IsConnected();
}

bool ScenePeerClient::sendMsg(uint16_t msgId, const char* data, uint16_t len)
{
    if (!m_client.IsConnected())
    {
        LOG_WARN("%s: send skipped, not connected msgId=0x%04X", m_peerName, msgId);
        return false;
    }
    return m_client.SendMsg(msgId, data, len);
}

void ScenePeerClient::setOnConnected(SceneUpstreamCallback::ConnectFn fn)
{
    m_callback.setOnConnect(std::move(fn));
}
