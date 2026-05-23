/**
 * @file    TcpClient.h
 * @brief  TCP 客户端连接器（用于服务器之间互联）
 *
 * 与 TcpServer 对称，但仅管理单条连接。
 * 内部维护独立的 epoll 实例，非阻塞 connect()。
 *
 * 典型场景：SessionServer 通过 TcpClient 连接 SuperServer。
 */

#pragma once
#include "TcpConnection.h"
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unordered_map>
#include <memory>
#include <string>

class TcpClient
{
public:
    /**
     * @brief 构造客户端
     * @param cb 事件回调
     */
    explicit TcpClient(INetCallback* cb)
        : m_cb(cb), m_epollFd(-1), m_conn(nullptr), m_connID(INVALID_CONN_ID)
    {}

    /** @brief 断开并释放资源 */
    ~TcpClient() { Disconnect(); }

    /**
     * @brief 非阻塞连接远端服务器
     * @param ip   目标 IP
     * @param port 目标端口
     * @return 成功发起连接返回 true
     * @note  连接完成事件通过 OnConnect 回调通知（可能在立即或在 Poll 中）
     */
    bool Connect(const std::string& ip, uint16_t port)
    {
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
        m_connID  = 1;
        m_conn    = std::make_shared<TcpConnection>(fd, m_connID, m_cb);

        epoll_event ev{};
        ev.events   = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
        ev.data.fd  = fd;
        ::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);

        if (ret == 0 && m_cb) m_cb->OnConnect(m_connID); /**< 本地立即连接成功 */
        return true;
    }

    /**
     * @brief 单帧驱动（必须主循环中调用）
     * @param timeout_ms epoll_wait 超时
     */
    void Poll(int timeout_ms = 10)
    {
        if (!m_conn || m_conn->IsClosed()) return;
        epoll_event events[16];
        int n = ::epoll_wait(m_epollFd, events, 16, timeout_ms);
        for (int i = 0; i < n; ++i)
        {
            if (events[i].events & EPOLLOUT)
            {
                // 非阻塞 connect 完成时 EPOLLOUT 就绪
                static bool connected = false;
                if (!connected)
                {
                    connected = true;
                    if (m_cb) m_cb->OnConnect(m_connID);
                }
                m_conn->OnWritable();
            }
            if (events[i].events & EPOLLIN) m_conn->OnReadable();
            if (events[i].events & (EPOLLERR | EPOLLHUP)) m_conn->Close();
        }
    }

    /**
     * @brief 发送消息
     * @see TcpConnection::SendMsg
     */
    bool SendMsg(uint16_t msgID, const char* data, uint16_t len)
    {
        if (!m_conn || m_conn->IsClosed()) return false;
        return m_conn->SendMsg(msgID, data, len);
    }

    /** @brief 断开连接 */
    void Disconnect()
    {
        if (m_conn) m_conn->Close();
        if (m_epollFd >= 0) { ::close(m_epollFd); m_epollFd = -1; }
    }

    /** @brief 连接是否存活 */
    bool IsConnected() const { return m_conn && !m_conn->IsClosed(); }

private:
    INetCallback*  m_cb;       /**< 事件回调 */
    int            m_epollFd;  /**< epoll 实例 fd */
    std::shared_ptr<TcpConnection> m_conn;  /**< 底层连接对象 */
    ConnID         m_connID;   /**< 连接 ID（固定为 1） */
};
