/**
 * @file    ExternalServerConnector.h
 * @brief   单条外联服 TcpClient 连接（含可选指数退避重连）
 */

#pragma once

#include "LoginServerList.h"
#include "MsgDispatcher.h"
#include "../net/NetDefine.h"
#include "../net/TcpClient.h"

#include <cstdint>
#include <string>

/**
 * @brief 将入站消息转交 MsgDispatcher（Gateway 连 LoginServer 注册口时使用）
 */
class DispatchingNetCallback : public INetCallback
{
public:
    void OnConnect(ConnID) override {}

    void OnDisconnect(ConnID) override {}

    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
    }
};

/**
 * @brief 空网络回调（外联出站连接无需处理入站业务时使用）
 */
class NullNetCallback : public INetCallback
{
public:
    void OnConnect(ConnID) override {}
    void OnDisconnect(ConnID) override {}
    void OnMessage(ConnID, uint8_t, uint8_t, const char*, uint16_t) override {}
};

/**
 * @brief 管理到单个外联服的 TcpClient 与重连策略
 */
class ExternalServerConnector
{
public:
    /**
     * @brief 构造外联连接器
     * @param cb 可选入站回调；nullptr 时使用内部 NullNetCallback
     */
    explicit ExternalServerConnector(INetCallback* cb = nullptr);

    /**
     * @brief 设置外联目标（来自 LoginServerList）
     * @param entry 外联配置；port==0 表示不连接
     */
    void setEntry(const ExternalServerEntry& entry);

    /** @brief 是否已配置有效目标（port>0 且 ip 非空） */
    bool isConfigured() const;

    /** @brief 是否 reconnect 标记为 true */
    bool wantsReconnect() const;

    /** @brief 连接是否存活 */
    bool isConnected() const;

    /** @brief 配置有效时发起连接（已连接则跳过） */
    void connectIfConfigured();

    /** @brief 驱动单连接 epoll（主循环 Poll(0)） */
    void poll();

    /**
     * @brief 断线重连节拍（reconnect==true 时按指数退避重试）
     * @param nowMs 当前毫秒时间戳（TimerMgr::NowMs）
     */
    void tickReconnect(uint64_t nowMs);

    /** @brief 底层 TcpClient（发协议用） */
    TcpClient& client();

    /** @brief 只读访问配置条目 */
    const ExternalServerEntry& entry() const;

private:
    ExternalServerEntry m_entry;       /**< 外联目标 */
    NullNetCallback     m_cb;          /**< 空回调 */
    TcpClient           m_client;      /**< 出站连接 */
    uint64_t            m_nextRetryMs; /**< 下次允许重连的时间戳 */
    uint32_t            m_retryDelayMs; /**< 当前重连间隔（指数退避） */
};
