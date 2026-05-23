/**
 * @file    TcpServer.h
 * @brief  基于 epoll 的 TCP 服务端（边缘触发模式）
 *
 * 特性：
 * - epoll ET + nonblock socket
 * - SO_REUSEADDR | SO_REUSEPORT 支持快速重启
 * - TCP_NODELAY 禁用 Nagle 算法
 * - 单线程轮询模型（Poll 为单帧驱动）
 * - 双索引查找连接：fd→conn + connID→conn
 *
 * @warning 仅适用于单线程场景，所有回调在 Poll() 调用栈内触发。
 */

#pragma once
#include "TcpConnection.h"
#include <sys/epoll.h>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <string>
#include <cstdio>

class TcpServer
{
public:
    /**
     * @brief 构造服务端
     * @param cb 必须非空，所有连接事件将回调到此接口
     */
    explicit TcpServer(INetCallback* cb)
        : m_cb(cb), m_epollFd(-1), m_listenFd(-1)
        , m_nextConnID(1), m_running(false)
    {}

    /** @brief 停止服务端并释放所有资源 */
    ~TcpServer() { Stop(); }

    /**
     * @brief 启动监听
     * @param ip   绑定 IP
     * @param port 绑定端口
     * @return 成功返回 true
     */
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

    /**
     * @brief 单帧驱动 —— 必须在主循环中反复调用
     * @param timeout_ms epoll_wait 超时时间（毫秒），默认 10ms
     *
     * 处理流程：
     * 1. epoll_wait 获取就绪事件
     * 2. 监听 fd 就绪 → AcceptAll()
     * 3. 连接 fd 就绪 → OnReadable / OnWritable
     */
    void Poll(int timeout_ms = 10)
    {
        epoll_event events[MAX_EPOLL_EVENTS];
        int n = ::epoll_wait(m_epollFd, events, MAX_EPOLL_EVENTS, timeout_ms);
        for (int i = 0; i < n; ++i)
        {
            int fd = events[i].data.fd;
            if (fd == m_listenFd)
            {
                AcceptAll();  /**< 监听 fd：处理新连接 */
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

    /**
     * @brief 向指定连接发送消息
     * @param id    目标连接 ID
     * @param msgID 协议号
     * @param data  消息体
     * @param len   消息体长度
     * @return 成功返回 true
     */
    bool SendMsg(ConnID id, uint16_t msgID, const char* data, uint16_t len)
    {
        auto it = m_connMap.find(id);
        if (it == m_connMap.end()) return false;
        return it->second->SendMsg(msgID, data, len);
    }

    /**
     * @brief 主动踢除连接
     * @param id 连接 ID
     */
    void Kick(ConnID id)
    {
        auto it = m_connMap.find(id);
        if (it != m_connMap.end())
        {
            it->second->Close();
            RemoveConn(it->second->GetFd());
        }
    }

    /**
     * @brief 停止服务端
     *
     * 关闭所有连接、epoll fd、监听 fd。
     */
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
    /**
     * @brief 创建并绑定监听 socket
     *
     * socket() → setsockopt(REUSEADDR|REUSEPORT|NODELAY) → bind() → listen()
     */
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

    /**
     * @brief 循环 accept 所有就绪连接（ET 模式必须循环到 EAGAIN）
     *
     * accept4(SOCK_NONBLOCK | SOCK_CLOEXEC) 避免额外 fcntl 调用。
     */
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

    /** @brief 向 epoll 实例注册 fd */
    void AddEpoll(int fd, uint32_t events)
    {
        epoll_event ev{};
        ev.events  = events;
        ev.data.fd = fd;
        ::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);
    }

    /** @brief 从 epoll 和索引表中删除连接 */
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

    INetCallback*  m_cb;  /**< 上层回调 */
    int            m_epollFd;     /**< epoll 实例 fd */
    int            m_listenFd;    /**< 监听 socket fd */
    uint32_t       m_nextConnID;  /**< 自增连接 ID 分配器 */
    bool           m_running;     /**< 运行状态标记 */

    /** @brief fd → TcpConnection 索引（用于 epoll 事件分发） */
    std::unordered_map<int,     std::shared_ptr<TcpConnection>> m_fdToConn;
    /** @brief ConnID → TcpConnection 索引（用于 SendMsg / Kick） */
    std::unordered_map<ConnID,  std::shared_ptr<TcpConnection>> m_connMap;
};
