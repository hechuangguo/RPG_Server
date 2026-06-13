/**
 * @file    GlobalHttpServer.h
 * @brief   GlobalServer HTTP 入站监听（原始 TCP + HttpParser）
 *
 * 职责：
 *   - 独立 epoll 监听 HTTP 端口，避免与游戏 MsgHeader 帧混用 TcpConnection
 *   - 每连接缓冲字节流，HttpParser 解析完整请求后回调业务 handler
 *   - 发送响应后关闭连接（Connection: close）
 *
 * 线程：仅 GlobalServer 主循环 Poll 驱动。
 */

#pragma once

#include "../sdk/http/HttpMessage.h"
#include "../sdk/net/NetDefine.h"

#include <functional>
#include <string>
#include <unordered_map>

/**
 * @brief HTTP 入站请求处理回调
 * @param connId 连接 ID（本服务内自增）
 * @param req    解析后的请求
 * @return 完整 HTTP 响应字节流（含头与 body）
 */
using HttpRequestHandler = std::function<std::string(ConnID connId, const HttpRequest& req)>;

/**
 * @brief GlobalServer 专用 HTTP 服务端
 */
class GlobalHttpServer
{
public:
    GlobalHttpServer();

    ~GlobalHttpServer();

    /**
     * @brief 绑定并开始监听
     * @param ip   监听 IP
     * @param port 监听端口；0 表示不调用
     * @return 成功 true
     */
    bool start(const std::string& ip, uint16_t port);

    /** @brief 停止监听并关闭所有连接 */
    void stop();

    /**
     * @brief 主循环单帧驱动
     * @param timeoutMs epoll_wait 超时（毫秒）
     */
    void poll(int timeoutMs = 10);

    /** @brief 是否已启动监听 */
    bool isRunning() const { return m_running; }

    /**
     * @brief 设置请求处理器
     * @param handler 收到完整 HTTP 请求时调用
     */
    void setHandler(HttpRequestHandler handler) { m_handler = std::move(handler); }

private:
    /**
     * @brief 单条 HTTP 连接状态
     */
    struct HttpConn
    {
        ConnID      id = 0;          /**< 连接 ID */
        int         fd = -1;           /**< socket fd */
        std::string recvBuf;           /**< 接收缓冲 */
        std::string sendBuf;           /**< 待发送响应 */
        bool        responseSent = false; /**< 已生成响应待发尽 */
    };

    int createListenSocket(const std::string& ip, uint16_t port);

    void acceptAll();

    void onReadable(int fd);

    void trySend(int fd, HttpConn& conn);

    void closeConn(int fd);

    void addEpoll(int fd, uint32_t events);

    HttpRequestHandler m_handler;     /**< 业务请求回调 */
    int              m_epollFd;       /**< epoll 实例 */
    int              m_listenFd;      /**< 监听 socket */
    uint32_t         m_nextConnId;    /**< 下一连接 ID */
    bool             m_running;       /**< 监听中 */
    std::unordered_map<int, HttpConn> m_conns; /**< fd → 连接状态 */
};
