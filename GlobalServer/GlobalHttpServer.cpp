/**
 * @file    GlobalHttpServer.cpp
 * @brief   GlobalServer HTTP 入站实现
 */

#include "GlobalHttpServer.h"

#include "../sdk/http/HttpCodec.h"
#include "../sdk/http/HttpParser.h"
#include "../sdk/net/NetDefine.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

GlobalHttpServer::GlobalHttpServer()
    : m_epollFd(-1)
    , m_listenFd(-1)
    , m_nextConnId(1)
    , m_running(false)
{
}

GlobalHttpServer::~GlobalHttpServer()
{
    stop();
}

int GlobalHttpServer::createListenSocket(const std::string& ip, uint16_t port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
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

void GlobalHttpServer::addEpoll(int fd, uint32_t events)
{
    epoll_event ev{};
    ev.events  = events;
    ev.data.fd = fd;
    ::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);
}

bool GlobalHttpServer::start(const std::string& ip, uint16_t port)
{
    if (port == 0)
        return false;
    m_listenFd = createListenSocket(ip, port);
    if (m_listenFd < 0)
        return false;
    m_epollFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0)
    {
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }
    addEpoll(m_listenFd, EPOLLIN | EPOLLET);
    m_running = true;
    return true;
}

void GlobalHttpServer::stop()
{
    m_running = false;
    for (auto& [fd, conn] : m_conns)
    {
        (void)conn;
        if (fd >= 0)
            ::close(fd);
    }
    m_conns.clear();
    if (m_epollFd >= 0)
    {
        ::close(m_epollFd);
        m_epollFd = -1;
    }
    if (m_listenFd >= 0)
    {
        ::close(m_listenFd);
        m_listenFd = -1;
    }
}

void GlobalHttpServer::acceptAll()
{
    while (true)
    {
        sockaddr_in clientAddr{};
        socklen_t   addrLen = sizeof(clientAddr);
        int cfd = ::accept4(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr),
                            &addrLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (cfd < 0)
            break;
        int opt = 1;
        ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        HttpConn conn;
        conn.id = m_nextConnId++;
        conn.fd = cfd;
        m_conns[cfd] = std::move(conn);
        addEpoll(cfd, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);
    }
}

void GlobalHttpServer::trySend(int fd, HttpConn& conn)
{
    while (!conn.sendBuf.empty())
    {
        ssize_t n = ::send(fd, conn.sendBuf.data(), conn.sendBuf.size(), MSG_NOSIGNAL);
        if (n > 0)
            conn.sendBuf.erase(0, static_cast<size_t>(n));
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        else
        {
            closeConn(fd);
            return;
        }
    }
    if (conn.responseSent && conn.sendBuf.empty())
        closeConn(fd);
}

void GlobalHttpServer::onReadable(int fd)
{
    auto it = m_conns.find(fd);
    if (it == m_conns.end())
        return;
    HttpConn& conn = it->second;
    char tmp[4096];
    while (true)
    {
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n > 0)
            conn.recvBuf.append(tmp, static_cast<size_t>(n));
        else if (n == 0)
        {
            closeConn(fd);
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            closeConn(fd);
            return;
        }
    }

    while (!conn.recvBuf.empty() && !conn.responseSent)
    {
        HttpRequest req;
        size_t      consumed = 0;
        HttpParseResult pr = HttpParser::parseRequest(conn.recvBuf.data(), conn.recvBuf.size(),
                                                      consumed, req);
        if (pr == HttpParseResult::NEED_MORE)
            break;
        if (pr == HttpParseResult::ERROR)
        {
            conn.sendBuf = HttpCodec::buildResponse(400, "Bad Request", "bad request");
            conn.responseSent = true;
            trySend(fd, conn);
            return;
        }
        conn.recvBuf.erase(0, consumed);
        if (m_handler)
            conn.sendBuf = m_handler(conn.id, req);
        else
            conn.sendBuf = HttpCodec::buildResponse(500, "Internal Server Error", "no handler");
        conn.responseSent = true;
        trySend(fd, conn);
        return;
    }
}

void GlobalHttpServer::closeConn(int fd)
{
    auto it = m_conns.find(fd);
    if (it == m_conns.end())
        return;
    ::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    m_conns.erase(it);
}

void GlobalHttpServer::poll(int timeoutMs)
{
    if (!m_running)
        return;
    epoll_event events[MAX_EPOLL_EVENTS];
    int n = ::epoll_wait(m_epollFd, events, MAX_EPOLL_EVENTS, timeoutMs);
    for (int i = 0; i < n; ++i)
    {
        int fd = events[i].data.fd;
        if (fd == m_listenFd)
        {
            acceptAll();
            continue;
        }
        auto it = m_conns.find(fd);
        if (it == m_conns.end())
            continue;
        if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
        {
            closeConn(fd);
            continue;
        }
        if (events[i].events & EPOLLIN)
            onReadable(fd);
        if (events[i].events & EPOLLOUT)
            trySend(fd, it->second);
    }
}
