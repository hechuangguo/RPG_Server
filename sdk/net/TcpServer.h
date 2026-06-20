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
 * accept 流程（AcceptAll）：
 * - 监听 socket 注册了 EPOLLIN | EPOLLET，epoll 触发时调用 AcceptAll()。
 * - AcceptAll() 循环调用 accept4(SOCK_NONBLOCK | SOCK_CLOEXEC) 直到返回错误（ET 模式必须循环）。
 * - 每个 accept 到的新 fd：
 *   1. 设置 TCP_NODELAY（减少小包延迟）
 *   2. 分配自增 ConnID
 *   3. 创建 TcpConnection（shared_ptr）
 *   4. 注册到 m_fdToConn（fd 索引）和 m_connMap（ConnID 索引）
 *   5. 注册到 epoll（EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP）
 *   6. 触发 OnConnect 回调
 *
 * 连接管理生命周期：
 * - 创建：AcceptAll() 中 accept4() 后创建 TcpConnection 并注册到索引
 * - 数据收发：Poll() 中 epoll 事件分发 → OnReadable() / OnWritable()
 * - 移除触发：对端关闭、错误、主动 Kick() → Close() + RemoveConn()
 * - RemoveConn() 从 epoll 删除 fd，并从 m_fdToConn 和 m_connMap 中移除条目
 * - 销毁：shared_ptr 引用计数归零后自动析构（~TcpConnection 关闭 fd）
 *
 * INetCallback 裸接口使用示例：
 * @code
 *   class MyServerCb : public INetCallback {
 *   public:
 *       void OnConnect(ConnID id) override {
 *           printf("client connected: %u\n", id);
 *       }
 *       void OnDisconnect(ConnID id) override {
 *           printf("client disconnected: %u\n", id);
 *       }
 *       void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override {
 *           printf("msg from %u: id=%u len=%u\n", id, msgID, len);
 *           // 处理消息...
 *       }
 *   };
 *
 *   MyServerCb cb;
 *   TcpServer server(&cb);
 *   server.Start("0.0.0.0", 8080);
 *   while (running) {
 *       server.Poll(10);
 *   }
 *   server.Stop();
 * @endcode
 *
 * @warning 仅适用于单线程场景，所有回调在 Poll() 调用栈内触发。
 */

#pragma once
#include "TcpConnection.h"
#include "TlsContext.h"
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <string>
#include <cstdio>

/**
 * @brief 基于 epoll 的 TCP 服务端（单线程、ET 模式）
 *
 * 管理监听 socket 和所有已连接的 TcpConnection。
 * 通过 INetCallback 回调接口将网络事件抛给上层业务。
 */
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
        , m_useTls(false)
    {}

    /** @brief 启用 TLS（须在 Start 之前调用；使用 TlsContext 服务端 SSL_CTX）
     *  @param requireClientCert true=区内/注册 mTLS；false=玩家客户端口单向 TLS
     */
    void EnableTls(bool requireClientCert = true)
    {
        if (TlsContext::instance().enabled())
        {
            m_useTls = true;
            m_tlsRequireClientCert = requireClientCert;
        }
    }

    /** @brief 是否已启用 TLS */
    bool tlsEnabled() const { return m_useTls; }

    /** @brief 析构时停止服务端并释放所有资源 */
    ~TcpServer() { Stop(); }

    /**
     * @brief 启动监听
     * @param ip   绑定 IP（如 "0.0.0.0" 监听所有网卡）
     * @param port 绑定端口号
     * @return 成功返回 true
     *
     * 启动流程：
     * 1. CreateListenSocket() 创建并配置监听 socket
     * 2. epoll_create1() 创建 epoll 实例
     * 3. 将监听 fd 注册到 epoll（EPOLLIN | EPOLLET）
     * 4. 设置 m_running = true
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
     * 1. epoll_wait 获取就绪事件（最多 MAX_EPOLL_EVENTS 个）
     * 2. 监听 fd 就绪 → AcceptAll() 循环 accept 新连接
     * 3. 连接 fd 就绪：
     *    - EPOLLERR / EPOLLHUP → Close() + RemoveConn()
     *    - EPOLLIN → OnReadable()（接收数据 + 拆包回调）
     *    - EPOLLOUT → OnWritable()（刷新发送缓冲区）
     *    - 回调后检查 IsClosed()，若已关闭则 RemoveConn()
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
                const uint32_t ev = events[i].events;

                /** 先处理 I/O / TLS 握手，再处理 hang-up（避免 EPOLLHUP 与 EPOLLIN 同批时跳过 tryFireConnect） */
                if (ev & EPOLLIN)  conn->OnReadable();
                if (ev & EPOLLOUT) conn->OnWritable();
                if (!conn->IsClosed())
                    conn->tryFireConnect();

                if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
                {
                    if (!conn->IsClosed())
                        conn->Close();
                }

                if (conn->IsClosed())
                    RemoveConn(fd);
            }
        }
    }

    /**
     * @brief 向指定连接发送消息
     * @param id    目标连接 ID（由 OnConnect 回调提供）
     * @param module 功能模块号
     * @param sub    子消息号
     */
    bool SendMsg(ConnID id, uint8_t module, uint8_t sub,
                 const char* data, uint16_t len)
    {
        auto it = m_connMap.find(id);
        if (it == m_connMap.end()) return false;
        return it->second->SendMsg(module, sub, data, len);
    }

    /** @brief 使用扁平协议号发送 */
    bool SendMsg(ConnID id, uint16_t flatMsgId, const char* data, uint16_t len)
    {
        auto it = m_connMap.find(id);
        if (it == m_connMap.end()) return false;
        return it->second->SendMsg(flatMsgId, data, len);
    }

    /** @brief 连接是否已触发 OnConnect（TLS 握手完成后为 true；OnDisconnect 回调内可查） */
    bool connectNotified(ConnID id) const
    {
        auto it = m_connMap.find(id);
        return it != m_connMap.end() && it->second->connectFired();
    }

    /**
     * @brief 主动踢除指定连接
     * @param id 连接 ID
     *
     * 执行流程：
     * 1. 通过 ConnID 查找 TcpConnection
     * 2. 调用 Close() 关闭连接（触发 OnDisconnect 回调）
     * 3. 调用 RemoveConn() 从 epoll 和索引中移除
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
     * 清理流程：
     * 1. 设置 m_running = false
     * 2. 遍历所有连接并 Close()（触发 OnDisconnect 回调）
     * 3. 清空 m_fdToConn 和 m_connMap
     * 4. 关闭 epoll fd 和监听 fd
     *
     * @note  连接的 shared_ptr 引用计数在索引清空后可能归零，触发析构
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
     * @brief 创建并配置监听 socket
     * @param ip   绑定 IP
     * @param port 绑定端口
     * @return 成功返回监听 fd；失败返回 -1
     *
     * 配置流程：
     * 1. socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC) 创建非阻塞 socket
     * 2. SO_REUSEADDR：允许 bind 处于 TIME_WAIT 状态的地址（快速重启）
     * 3. SO_REUSEPORT：允许多个进程/线程绑定同一地址（负载均衡）
     * 4. TCP_NODELAY：禁用 Nagle 算法（减少小包延迟）
     * 5. bind() + listen(LISTEN_BACKLOG)
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
     * 对每个新连接：
     * 1. accept4(SOCK_NONBLOCK | SOCK_CLOEXEC) 接受连接
     * 2. 设置 TCP_NODELAY
     * 3. 分配自增 ConnID
     * 4. 创建 TcpConnection 并注册到双索引
     * 5. 注册到 epoll（EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP）
     * 6. 触发 OnConnect 回调通知上层
     *
     * @note  EPOLLOUT 初始就绪：用于检测非阻塞 connect（此处 accept 的 fd 已完成连接），
     *        后续 EPOLLOUT 事件表示发送缓冲区可写。
     *        EPOLLRDHUP：对端关闭写端时触发，配合 EPOLLIN 检测半关闭状态。
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
            SSL* ssl = m_useTls
                ? TlsContext::instance().newServerSsl(cfd, m_tlsRequireClientCert)
                : nullptr;
            auto conn = std::make_shared<TcpConnection>(cfd, id, m_cb, ssl, true);
            m_fdToConn[cfd]  = conn;
            m_connMap[id]    = conn;
            AddEpoll(cfd, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);
            /** 明文：立即 OnConnect；TLS：握手完成后由 tryFireConnect() 触发 */
            if (!m_useTls && m_cb)
                m_cb->OnConnect(id);
        }
    }

    /**
     * @brief 向 epoll 实例注册 fd
     * @param fd      要注册的文件描述符
     * @param events  关注的事件掩码
     */
    void AddEpoll(int fd, uint32_t events)
    {
        epoll_event ev{};
        ev.events  = events;
        ev.data.fd = fd;
        ::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);
    }

    /**
     * @brief 从 epoll 和双索引中删除连接
     * @param fd 要移除的 socket fd
     *
     * 执行流程：
     * 1. epoll_ctl(EPOLL_CTL_DEL) 从 epoll 实例中移除 fd
     * 2. 通过 fd 在 m_fdToConn 中查找连接，获取 ConnID
     * 3. 从 m_connMap 和 m_fdToConn 中移除条目
     *
     * @note  移除后连接的 shared_ptr 引用计数减 1，若归零则析构（~TcpConnection 关闭 fd）
     */
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
    INetCallback*  m_cb;  /**< 上层回调接口（不负责释放） */
    int            m_epollFd;     /**< epoll 实例 fd */
    int            m_listenFd;    /**< 监听 socket fd */
    uint32_t       m_nextConnID;  /**< 自增连接 ID 分配器（从 1 开始） */
    bool           m_running;     /**< 运行状态标记 */
    bool           m_useTls;      /**< 是否对新连接启用 TLS */
    bool           m_tlsRequireClientCert = true; /**< accept 是否要求对端证书 */
    /** @brief fd → TcpConnection 索引（用于 epoll 事件分发，O(1) 查找） */
    std::unordered_map<int,     std::shared_ptr<TcpConnection>> m_fdToConn;
    /** @brief ConnID → TcpConnection 索引（用于 SendMsg / Kick，O(1) 查找） */
    std::unordered_map<ConnID,  std::shared_ptr<TcpConnection>> m_connMap;
};
