#pragma once
#include "TcpConnection.h"
#include <sys/epoll.h>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <string>
#include <cstdio>

// ============================================================
//  基于 epoll 的 TCP 服务端（ET 模式，单线程）
// ============================================================
class TcpServer
{
public:
    explicit TcpServer(INetCallback* cb)
        : m_cb(cb), m_epollFd(-1), m_listenFd(-1)
        , m_nextConnID(1), m_running(false)
    {}
    ~TcpServer() { Stop(); }

    bool Start(const std::string& ip, uint16_t port)
    {
        m_listenFd = CreateListenSocket(ip, port);
        if (m_listenFd < 0) return false;

        m_epollFd = ::epoll_create1(EPOLL_CLOEXEC);
        if (m_epollFd < 0) { ::close(m_listenFd); return false; }

        AddEpoll(m_listenFd, EPOLLIN | EPOLLET);
        m_running = true;
        return true;
    }

    // 单帧驱动，在主循环中调用（timeout_ms: epoll_wait 超时）
    void Poll(int timeout_ms = 10)
    {
        epoll_event events[MAX_EPOLL_EVENTS];
        int n = ::epoll_wait(m_epollFd, events, MAX_EPOLL_EVENTS, timeout_ms);
        for (int i = 0; i < n; ++i)
        {
            int fd = events[i].data.fd;
            if (fd == m_listenFd)
            {
                AcceptAll();
            }
            else
            {
                auto it = m_fdToConn.find(fd);
                if (it == m_fdToConn.end()) continue;
                auto& conn = it->second;
                if (events[i].events & (EPOLLERR | EPOLLHUP))
                {
                    conn->Close();
                    RemoveConn(fd);
                }
                else
                {
                    if (events[i].events & EPOLLIN)  conn->OnReadable();
                    if (events[i].events & EPOLLOUT) conn->OnWritable();
                    if (conn->IsClosed()) RemoveConn(fd);
                }
            }
        }
    }

    // 发送消息
    bool SendMsg(ConnID id, uint16_t msgID, const char* data, uint16_t len)
    {
        auto it = m_connMap.find(id);
        if (it == m_connMap.end()) return false;
        return it->second->SendMsg(msgID, data, len);
    }

    void Kick(ConnID id)
    {
        auto it = m_connMap.find(id);
        if (it != m_connMap.end())
        {
            it->second->Close();
            RemoveConn(it->second->GetFd());
        }
    }

    void Stop()
    {
        m_running = false;
        for (auto& [fd, conn] : m_fdToConn) conn->Close();
        m_fdToConn.clear();
        m_connMap.clear();
        if (m_epollFd >= 0) { ::close(m_epollFd); m_epollFd = -1; }
        if (m_listenFd >= 0){ ::close(m_listenFd); m_listenFd = -1; }
    }

private:
    int CreateListenSocket(const std::string& ip, uint16_t port)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) return -1;
        int opt = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,  &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
            ::listen(fd, LISTEN_BACKLOG) < 0)
        {
            ::close(fd);
            return -1;
        }
        return fd;
    }

    void AcceptAll()
    {
        while (true)
        {
            sockaddr_in clientAddr{};
            socklen_t   addrLen = sizeof(clientAddr);
            int cfd = ::accept4(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr),
                                &addrLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (cfd < 0) break;
            int opt = 1;
            ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
            ConnID id = m_nextConnID++;
            auto conn = std::make_shared<TcpConnection>(cfd, id, m_cb);
            m_fdToConn[cfd]  = conn;
            m_connMap[id]    = conn;
            AddEpoll(cfd, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);
            if (m_cb) m_cb->OnConnect(id);
        }
    }

    void AddEpoll(int fd, uint32_t events)
    {
        epoll_event ev{};
        ev.events  = events;
        ev.data.fd = fd;
        ::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);
    }

    void RemoveConn(int fd)
    {
        ::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
        auto it = m_fdToConn.find(fd);
        if (it != m_fdToConn.end())
        {
            ConnID id = it->second->GetID();
            m_connMap.erase(id);
            m_fdToConn.erase(it);
        }
    }

    INetCallback*  m_cb;
    int            m_epollFd;
    int            m_listenFd;
    uint32_t       m_nextConnID;
    bool           m_running;
    std::unordered_map<int,     std::shared_ptr<TcpConnection>> m_fdToConn;
    std::unordered_map<ConnID,  std::shared_ptr<TcpConnection>> m_connMap;
};
