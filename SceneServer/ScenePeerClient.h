/**
 * @file    ScenePeerClient.h
 * @brief   SceneServer 出站 TcpClient 基类与统一入站回调
 *
 * 出站连接使用独立 INetCallback，避免与 SceneServer 入站 Gateway 连接混用 connId。
 */

#pragma once

#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/log/Logger.h"

#include <functional>
#include <string>

/** @brief SceneServer 出站 TcpClient 统一消息派发（不占用 Gateway 入站 connId） */
class SceneUpstreamCallback : public INetCallback
{
public:
    /** @brief 连接建立时可选回调（用于 flush 待注册队列等） */
    using ConnectFn = std::function<void()>;

    /** @brief 设置 OnConnect 时触发的回调 */
    void setOnConnect(ConnectFn fn) { onConnect = std::move(fn); }

    void OnConnect(ConnID) override
    {
        if (onConnect)
            onConnect();
    }

    void OnDisconnect(ConnID) override {}

    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
    }

private:
    ConnectFn onConnect;
};

/**
 * @brief SceneServer 到单个区内服的出站连接封装
 */
class ScenePeerClient
{
public:
    explicit ScenePeerClient(const char* peerName);

    /** @brief 发起 TCP 连接 */
    bool connect(const std::string& ip, uint16_t port);

    /** @brief 驱动 epoll（主循环 Poll(0)） */
    void poll();

    /** @brief 连接是否存活 */
    bool isConnected() const;

    /**
     * @brief 发送内部协议消息
     * @return 已连接且发送成功 true；未连接时 WARN 并 false
     */
    bool sendMsg(uint16_t msgId, const char* data, uint16_t len);

    /** @brief 设置连接就绪回调 */
    void setOnConnected(SceneUpstreamCallback::ConnectFn fn);

    /** @brief 底层 TcpClient（Super 等仍可直接使用时可参考） */
    TcpClient& client() { return m_client; }

protected:
    SceneUpstreamCallback m_callback; /**< 出站 INetCallback */
    TcpClient             m_client;   /**< 出站 TcpClient */

private:
    const char* m_peerName; /**< 日志用对端名 */
};
