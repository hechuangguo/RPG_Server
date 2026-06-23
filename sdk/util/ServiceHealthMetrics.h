/**
 * @file    ServiceHealthMetrics.h
 * @brief   进程内轻量健康指标（登录链路 SLI、发送失败、重连次数）
 */

#pragma once

#include <atomic>
#include <cstdint>

class ServiceHealthMetrics
{
public:
    static ServiceHealthMetrics& instance()
    {
        static ServiceHealthMetrics inst;
        return inst;
    }

    void incLoginAuthSuccess() { ++loginAuthSuccess; }
    void incLoginAuthFail() { ++loginAuthFail; }
    void incSendMsgFail() { ++sendMsgFail; }
    void incReconnectAttempt() { ++reconnectAttempt; }
    void incRateLimitHit() { ++rateLimitHit; }
    void setPendingLoginCount(uint64_t n) { pendingLoginCount.store(n); }
    void setOutboxDepth(uint64_t n) { outboxDepth.store(n); }

    uint64_t getLoginAuthSuccess() const { return loginAuthSuccess.load(); }
    uint64_t getLoginAuthFail() const { return loginAuthFail.load(); }
    uint64_t getSendMsgFail() const { return sendMsgFail.load(); }
    uint64_t getReconnectAttempt() const { return reconnectAttempt.load(); }
    uint64_t getRateLimitHit() const { return rateLimitHit.load(); }
    uint64_t getPendingLoginCount() const { return pendingLoginCount.load(); }
    uint64_t getOutboxDepth() const { return outboxDepth.load(); }

private:
    ServiceHealthMetrics() = default;
    std::atomic<uint64_t> loginAuthSuccess{0};
    std::atomic<uint64_t> loginAuthFail{0};
    std::atomic<uint64_t> sendMsgFail{0};
    std::atomic<uint64_t> reconnectAttempt{0};
    std::atomic<uint64_t> rateLimitHit{0};
    std::atomic<uint64_t> pendingLoginCount{0};
    std::atomic<uint64_t> outboxDepth{0};
};
