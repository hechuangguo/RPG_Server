/**
 * @file    InternalServerConnector.h
 * @brief   区内 TcpClient 统一重连（指数退避 + 半开检测）
 */
#pragma once
#include "../net/TcpClient.h"
#include "../net/NetTls.h"
#include "../log/Logger.h"
#include "ServiceHealthMetrics.h"
#include "LoginFlowTimeouts.h"
#include <algorithm>
#include <cstdint>
#include <string>

class InternalServerConnector
{
public:
    static constexpr uint32_t MIN_RETRY_MS = 1000;
    static constexpr uint32_t MAX_RETRY_MS = 30000;

    explicit InternalServerConnector(INetCallback* cb, const char* logName)
        : client(cb), logName(logName ? logName : "InternalServer") {}

    TcpClient& tcpClient() { return client; }

    void setTarget(std::string ip, uint16_t port)
    {
        targetIp = std::move(ip);
        targetPort = port;
    }

    bool isConfigured() const { return !targetIp.empty() && targetPort > 0; }

    bool connectNow()
    {
        if (!isConfigured())
            return false;
        client.Disconnect();
        wireTlsClient(client);
        return client.Connect(targetIp, targetPort);
    }

    void tickReconnect(uint64_t nowMs)
    {
        if (!isConfigured())
            return;
        if (client.IsConnected())
        {
            if (client.canSend())
            {
                tlsStuckSinceMs = 0;
                retryDelayMs = MIN_RETRY_MS;
                return;
            }
            if (tlsStuckSinceMs == 0)
                tlsStuckSinceMs = nowMs;
            if (nowMs - tlsStuckSinceMs < EXTERNAL_TLS_STUCK_MS)
                return;
            LOG_WARN("%s TLS 半开连接强制断开: %s:%u", logName, targetIp.c_str(), targetPort);
            client.Disconnect();
            tlsStuckSinceMs = 0;
        }
        if (nowMs < nextRetryMs)
            return;
        ServiceHealthMetrics::instance().incReconnectAttempt();
        client.Disconnect();
        wireTlsClient(client);
        if (!client.Connect(targetIp, targetPort))
        {
            LOG_WARN("%s 重连失败: %s:%u", logName, targetIp.c_str(), targetPort);
            retryDelayMs = std::min(retryDelayMs * 2, MAX_RETRY_MS);
        }
        else
        {
            LOG_INFO("%s 重连已发起: %s:%u", logName, targetIp.c_str(), targetPort);
            retryDelayMs = MIN_RETRY_MS;
        }
        nextRetryMs = nowMs + retryDelayMs;
    }

private:
    TcpClient client;
    const char* logName;
    std::string targetIp;
    uint16_t targetPort = 0;
    uint32_t retryDelayMs = MIN_RETRY_MS;
    uint64_t nextRetryMs = 0;
    uint64_t tlsStuckSinceMs = 0;
};
