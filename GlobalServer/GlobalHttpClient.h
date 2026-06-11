/**
 * @file    GlobalHttpClient.h
 * @brief   GlobalServer HTTP 出站客户端（原始 TCP + 重连 + 响应解析）
 *
 * 职责：
 *   - 按 extern_global.xml Http Client 配置连接远端 HTTP 服务
 *   - 发送 GET 探测；解析响应并写日志
 *   - reconnect=1 时断线指数退避重连（1s～30s）
 *
 * 启用条件：enabled=true 且 host/port 有效（见 HttpClientConfig）。
 */

#pragma once

#include "../sdk/http/HttpMessage.h"
#include "../sdk/util/ExternServerConfig.h"

#include <cstdint>
#include <string>

/**
 * @brief GlobalServer 对外 HTTP 客户端
 */
class GlobalHttpClient
{
public:
    GlobalHttpClient();

    ~GlobalHttpClient();

    /**
     * @brief 从配置填充目标地址与重连策略
     * @param cfg Http Client 段（含 enabled）
     */
    void configure(const HttpClientConfig& cfg);

    /** @brief enabled 且 host/port 有效 */
    bool isConfigured() const;

    /** @brief 配置要求断线重连 */
    bool wantsReconnect() const;

    /** @brief TCP 连接是否存活 */
    bool isConnected() const;

    /** @brief 首次连接（isConfigured 为 true 时） */
    void connectIfConfigured();

    /**
     * @brief 主循环单帧驱动
     * @param timeoutMs epoll_wait 超时
     */
    void poll(int timeoutMs = 0);

    /**
     * @brief 断线指数退避重连
     * @param nowMs 当前毫秒时间戳（TimerMgr::NowMs）
     */
    void tickReconnect(uint64_t nowMs);

    /**
     * @brief 发送 GET 请求
     * @param path 路径（如 /health）
     * @return 已写入发送缓冲 true
     */
    bool sendGet(const char* path);

private:
    bool doConnect();

    void disconnect();

    void onReadable();

    void trySend();

    HttpClientConfig m_cfg;           /**< 目标与 enabled/reconnect */
    int              m_fd;            /**< 连接 socket，-1 未连接 */
    int              m_epollFd;       /**< 独立 epoll */
    bool             m_connectNotified; /**< 是否已打连接成功日志 */
    std::string      m_recvBuf;       /**< 响应接收缓冲 */
    std::string      m_sendBuf;       /**< 待发请求 */
    uint64_t         m_nextRetryMs;   /**< 下次重连时刻 */
    uint32_t         m_retryDelayMs;  /**< 当前重连退避间隔 */
};
