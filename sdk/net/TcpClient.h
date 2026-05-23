#pragma once
#include "TcpConnection.h"
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unordered_map>
#include <memory>
#include <string>

// ============================================================
//  TCP 客户端连接器（用于服务器之间互联）
// ============================================================
class TcpClient
{
public:
    explicit TcpClient(INetCallback* cb)
        : m_cb(cb), m_epollFd(-1), m_conn(nullptr), m_connID(INVALID_CONN_ID)
    {}
    ~TcpClient() { Disconnect(); }

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

        if (ret == 0 && m_cb) m_cb->OnConnect(m_connID);
        return true;
    }

    void Poll(int timeout_ms = 10)
    {
        if (!m_conn || m_conn->IsClosed()) return;
        epoll_event events[16];
        int n = ::epoll_wait(m_epollFd, events, 16, timeout_ms);
        for (int i = 0; i < n; ++i)
        {
            if (events[i].events & EPOLLOUT)
            {
                // 连接完成
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

    bool SendMsg(uint16_t msgID, const char* data, uint16_t len)
    {
        if (!m_conn || m_conn->IsClosed()) return false;
        return m_conn->SendMsg(msgID, data, len);
    }

    void Disconnect()
    {
        if (m_conn) m_conn->Close();
        if (m_epollFd >= 0) { ::close(m_epollFd); m_epollFd = -1; }
    }

    bool IsConnected() const { return m_conn && !m_conn->IsClosed(); }

private:
    INetCallback*  m_cb;
    int            m_epollFd;
    std::shared_ptr<TcpConnection> m_conn;
    ConnID         m_connID;
};
