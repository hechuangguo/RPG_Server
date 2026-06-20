/**
 * @file    ExternalServerConnector.cpp
 * @brief   外联服 TcpClient 连接与重连实现
 */

#include "ExternalServerConnector.h"

#include "../log/Logger.h"
#include "../net/NetTls.h"
#include "../timer/TimerMgr.h"

#include <algorithm>

namespace
{
constexpr uint32_t MIN_RETRY_MS = 1000;
constexpr uint32_t MAX_RETRY_MS = 30000;

const char* externalServerName(SubServerType type)
{
    switch (type)
    {
    case SubServerType::LOGGER: return "LoggerServer";
    case SubServerType::GLOBAL: return "GlobalServer";
    case SubServerType::ZONE:   return "ZoneServer";
    case SubServerType::LOGIN:  return "LoginServer";
    default:                    return "ExternalServer";
    }
}
}  // namespace

ExternalServerConnector::ExternalServerConnector(INetCallback* cb)
    : m_client(cb ? cb : &m_cb)
    , m_nextRetryMs(0)
    , m_retryDelayMs(MIN_RETRY_MS)
{
}

void ExternalServerConnector::setEntry(const ExternalServerEntry& entry)
{
    m_entry = entry;
}

bool ExternalServerConnector::isConfigured() const
{
    return m_entry.port > 0 && !m_entry.ip.empty();
}

bool ExternalServerConnector::wantsReconnect() const
{
    return m_entry.reconnect;
}

bool ExternalServerConnector::isConnected() const
{
    return m_client.IsConnected();
}

void ExternalServerConnector::connectIfConfigured()
{
    if (!isConfigured() || m_client.IsConnected())
        return;
    m_client.Disconnect();
    wireTlsClient(m_client);
    m_client.Connect(m_entry.ip, m_entry.port);
}

void ExternalServerConnector::poll()
{
    m_client.Poll(0);
}

void ExternalServerConnector::tickReconnect(uint64_t nowMs)
{
    if (!isConfigured() || !m_entry.reconnect)
        return;
    if (m_client.IsConnected())
    {
        m_retryDelayMs = MIN_RETRY_MS;
        return;
    }
    if (nowMs < m_nextRetryMs)
        return;

    m_client.Disconnect();
    wireTlsClient(m_client);
    if (!m_client.Connect(m_entry.ip, m_entry.port))
    {
        LOG_DEBUG("外联 TCP 连接失败: %s %s:%u（进程未启动时可忽略）",
                  externalServerName(m_entry.type), m_entry.ip.c_str(), m_entry.port);
        m_retryDelayMs = std::min(m_retryDelayMs * 2, MAX_RETRY_MS);
    }
    m_nextRetryMs = nowMs + m_retryDelayMs;
}

TcpClient& ExternalServerConnector::client()
{
    return m_client;
}

const ExternalServerEntry& ExternalServerConnector::entry() const
{
    return m_entry;
}
