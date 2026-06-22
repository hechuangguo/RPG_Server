/**
 * @file    TcpClient.h
 * @brief  TCP 客户端连接器（用于服务器之间互联）
 *
 * 与 TcpServer 对称，但仅管理单条连接。
 * 内部维护独立的 epoll 实例，非阻塞 connect()。
 *
 * 典型场景：SessionServer 通过 TcpClient 连接 SuperServer。
 *
 * 重连机制：
 * - 本类**不内置自动重连**。若连接断开，IsConnected() 返回 false，
 *   调用方需根据业务需求自行实现重连逻辑（如指数退避重试）。
 * - 重连前需先调用 Disconnect() 释放旧连接，再调用 Connect() 发起新连接。
 * - 建议重连模式示例：
 * @code
 *   void TryReconnect(TcpClient& client, const std::string& ip, uint16_t port) {
 *       client.Disconnect();
 *       while (!client.Connect(ip, port)) {
 *           std::this_thread::sleep_for(std::chrono::seconds(1));
 *       }
 *   }
 * @endcode
 *
 * 消息发送队列：
 * - 本身不维护发送队列，发送操作直接委托给内部的 TcpConnection。
 * - TcpConnection 内部有一个 RingBuffer 作为发送缓冲区（m_sendBuf），
 *   SendMsg() 仅将消息头+消息体写入缓冲区，实际发送由 Poll() 中的
 *   EPOLLOUT 事件驱动 OnWritable() 异步完成。
 * - 若发送缓冲区已满，SendMsg() 返回 false，调用方可选择丢弃或缓存。
 *
 * 异步回调流程（INetCallback）：
 * - OnConnect(connID)：连接建立成功时触发。
 *   - 若 connect() 返回 0（本地立即成功），在 Connect() 调用栈内触发。
 *   - 若 connect() 返回 EINPROGRESS（异步进行中），在 Poll() 检测到 EPOLLOUT 时触发。
 * - OnMessage(connID, msgID, data, len)：收到完整消息时触发（在 Poll() → OnReadable() 调用栈内）。
 * - OnDisconnect(connID)：连接断开时触发（包括对端关闭、错误关闭、主动 Disconnect()）。
 *
 * 线程模型：
 * - 所有回调均在 Poll() 调用栈内同步触发，**非线程安全**。
 * - 避免在回调中执行长时间阻塞操作。
 */

#pragma once
#include "TcpConnection.h"
#include "TlsContext.h"
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <unordered_map>
#include <memory>
#include <string>

/**
 * @brief TCP 客户端（单连接，用于服务器间互联）
 *
 * 管理一条到远端服务器的非阻塞 TCP 连接。
 * 内部持有独立的 epoll 实例和唯一的 TcpConnection。
 *
 * 生命周期：Connect() → Poll() 循环 → Disconnect()
 */
class TcpClient
{
public:
    /**
     * @brief 构造客户端
     * @param cb 事件回调接口（INetCallback），生命周期由调用方管理
     */
    explicit TcpClient(INetCallback* cb)
        : m_cb(cb), m_epollFd(-1), m_conn(nullptr), m_connID(INVALID_CONN_ID)
        , m_useTls(false), m_tcpConnectDone(false), m_connectNotified(false)
    {}

    /** @brief 启用 TLS（须在 Connect 之前调用） */
    void EnableTls()
    {
        if (TlsContext::instance().enabled())
            m_useTls = true;
    }

    /** @brief 析构时自动断开连接并释放资源 */
    ~TcpClient() { Disconnect(); }

    /**
     * @brief 非阻塞连接远端服务器
     * @param ip   目标 IP 地址（如 "127.0.0.1"）
     * @param port 目标端口号
     * @return 成功发起连接返回 true（注意：此时连接可能尚未真正建立）
     *
     * 连接流程：
     * 1. 创建非阻塞 socket（SOCK_NONBLOCK | SOCK_CLOEXEC）
     * 2. 设置 TCP_NODELAY 禁用 Nagle 算法（减少小包延迟）
     * 3. 调用 connect()，若返回 EINPROGRESS 表示异步连接进行中
     * 4. 创建 epoll 实例，监听 EPOLLIN | EPOLLOUT（ET 模式）
     * 5. 若 connect() 立即返回 0，直接触发 OnConnect 回调
     *
     * @note  连接真正建立的事件通过 OnConnect 回调通知（可能立即或在 Poll 中）
     */
    bool Connect(const std::string& ip, uint16_t port)
    {
        Disconnect();
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) return false;
        int opt = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        int ret = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (ret < 0 && errno != EINPROGRESS) { ::close(fd); return false; }
        m_epollFd = ::epoll_create1(EPOLL_CLOEXEC);
        if (m_epollFd < 0) { ::close(fd); return false; }
        m_connID  = 1;
        m_tcpConnectDone = (ret == 0);
        m_connectNotified = false;
        SSL* ssl = nullptr;
        if (m_useTls)
        {
            ssl = TlsContext::instance().newClientSsl(fd);
            if (!ssl)
            {
                ::close(fd);
                ::close(m_epollFd);
                m_epollFd = -1;
                return false;
            }
        }
        m_conn    = std::make_shared<TcpConnection>(fd, m_connID, m_cb, ssl, false);
        epoll_event ev{};
        ev.events   = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
        ev.data.fd  = fd;
        ::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);
        if (ret == 0)
        {
            m_tcpConnectDone = true;
            if (!m_useTls)
            {
                m_conn->tryFireConnect();
                m_connectNotified = true;
            }
        }
        return true;
    }

    /**
     * @brief 单帧驱动（必须主循环中调用）
     * @param timeout_ms epoll_wait 超时时间（毫秒），默认 10ms
     *
     * 处理流程：
     * 1. epoll_wait 等待事件（最多 16 个）
     * 2. EPOLLIN 事件：调用 OnReadable() 接收数据并拆包
     * 3. EPOLLOUT 事件：刷新发送缓冲区（须在 IN 之后，便于 TLS read-then-write）
     * 4. EPOLLERR / EPOLLHUP / EPOLLRDHUP：先完成 I/O 与 TLS 握手，再 Close
     */
    void Poll(int timeout_ms = 10)
    {
        if (!m_conn || m_conn->IsClosed()) return;
        epoll_event events[16];
        int n = ::epoll_wait(m_epollFd, events, 16, timeout_ms);
        for (int i = 0; i < n; ++i)
        {
            const uint32_t ev = events[i].events;

            if (ev & EPOLLIN)
                m_conn->OnReadable();
            if (ev & EPOLLOUT)
            {
                if (!m_tcpConnectDone)
                    m_tcpConnectDone = true;
                /** 握手 WANT_WRITE 或发送区有数据时需 OnWritable；应用数据 IN 已刷过则 OUT 可跳过 */
                if (m_conn && !m_conn->IsClosed() &&
                    (m_conn->isTlsHandshaking() || m_conn->hasPendingSend()))
                    m_conn->OnWritable();
            }
            else if (m_conn && !m_conn->IsClosed() &&
                     (m_conn->isTlsHandshaking() || m_conn->hasPendingSend()))
            {
                /** 无 EPOLLOUT（如 ET 漏边）：补刷 TLS 握手或待发数据 */
                m_conn->OnWritable();
            }
            if (!m_connectNotified && m_conn && !m_conn->IsClosed())
            {
                m_conn->tryFireConnect();
                if (m_conn->connectFired())
                    m_connectNotified = true;
            }

            if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
            {
                if (m_conn && !m_conn->IsClosed())
                    m_conn->Close();
            }
        }
    }

    /**
     * @brief 向远端发送消息
     * @param module 功能模块号
     * @param sub    子消息号
     */
    bool SendMsg(uint8_t module, uint8_t sub, const char* data, uint16_t len)
    {
        if (!m_conn || m_conn->IsClosed()) return false;
        return m_conn->SendMsg(module, sub, data, len);
    }

    /** @brief 使用扁平协议号发送 */
    bool SendMsg(uint16_t flatMsgId, const char* data, uint16_t len)
    {
        if (!m_conn || m_conn->IsClosed()) return false;
        return m_conn->SendMsg(flatMsgId, data, len);
    }

    /**
     * @brief 断开连接并释放 epoll 资源
     *
     * 关闭底层 TcpConnection（触发 OnDisconnect 回调），
     * 并关闭 epoll 实例 fd。之后可再次调用 Connect() 发起新连接。
     */
    void Disconnect()
    {
        if (m_conn) m_conn->Close();
        if (m_epollFd >= 0) { ::close(m_epollFd); m_epollFd = -1; }
        m_conn = nullptr;
        m_tcpConnectDone = false;
        m_connectNotified = false;
    }

    /** @brief 连接是否存活（未关闭） */
    bool IsConnected() const { return m_conn && !m_conn->IsClosed(); }

    /** @brief TLS 握手是否完成、连接可 SendMsg（明文 TCP 连通后即可） */
    bool canSend() const
    {
        return m_conn && !m_conn->IsClosed() && m_conn->isTlsReady();
    }

private:
    INetCallback*  m_cb;       /**< 事件回调接口（不负责释放） */
    int            m_epollFd;  /**< 独立的 epoll 实例 fd */
    std::shared_ptr<TcpConnection> m_conn;  /**< 底层连接对象（shared_ptr 管理生命周期） */
    ConnID         m_connID;   /**< 连接 ID（客户端固定为 1） */
    bool           m_useTls;   /**< 出站是否 TLS */
    bool           m_tcpConnectDone;  /**< TCP connect 是否完成 */
    bool           m_connectNotified;   /**< OnConnect 是否已触发 */
};
