/**
 * @file    GlobalHttpClient.cpp
 * @brief   GlobalServer HTTP 出站客户端实现
 */

#include "GlobalHttpClient.h"

#include "../sdk/http/HttpCodec.h"
#include "../sdk/http/HttpParser.h"
#include "../sdk/log/Logger.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
constexpr uint32_t MIN_RETRY_MS = 1000;
constexpr uint32_t MAX_RETRY_MS = 30000;
}  // namespace

GlobalHttpClient::GlobalHttpClient()
    : m_fd(-1)
    , m_epollFd(-1)
    , m_connectNotified(false)
    , m_nextRetryMs(0)
    , m_retryDelayMs(MIN_RETRY_MS)
{
}

GlobalHttpClient::~GlobalHttpClient()
{
    disconnect();
}

void GlobalHttpClient::configure(const HttpClientConfig& cfg)
{
    m_cfg = cfg;
}

bool GlobalHttpClient::isConfigured() const
{
    return m_cfg.enabled && m_cfg.port > 0 && !m_cfg.host.empty();
}

bool GlobalHttpClient::wantsReconnect() const
{
    return m_cfg.reconnect;
}

bool GlobalHttpClient::isConnected() const
{
    return m_fd >= 0;
}

void GlobalHttpClient::disconnect()
{
    if (m_fd >= 0)
    {
        if (m_epollFd >= 0)
            ::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, m_fd, nullptr);
        ::close(m_fd);
        m_fd = -1;
    }
    if (m_epollFd >= 0)
    {
        ::close(m_epollFd);
        m_epollFd = -1;
    }
    m_connectNotified = false;
    m_recvBuf.clear();
    m_sendBuf.clear();
}

bool GlobalHttpClient::doConnect()
{
    disconnect();
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    int opt = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(m_cfg.port);
    ::inet_pton(AF_INET, m_cfg.host.c_str(), &addr.sin_addr);
    int ret = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS)
    {
        ::close(fd);
        return false;
    }
    m_epollFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0)
    {
        ::close(fd);
        return false;
    }
    m_fd = fd;
    epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    ev.data.fd = fd;
    ::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &ev);
    if (ret == 0)
        m_connectNotified = true;
    return true;
}

void GlobalHttpClient::connectIfConfigured()
{
    if (!isConfigured() || isConnected())
        return;
    doConnect();
}

void GlobalHttpClient::trySend()
{
    if (m_fd < 0)
        return;
    while (!m_sendBuf.empty())
    {
        ssize_t n = ::send(m_fd, m_sendBuf.data(), m_sendBuf.size(), MSG_NOSIGNAL);
        if (n > 0)
            m_sendBuf.erase(0, static_cast<size_t>(n));
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        else
        {
            disconnect();
            return;
        }
    }
}

void GlobalHttpClient::onReadable()
{
    if (m_fd < 0)
        return;
    char tmp[4096];
    while (true)
    {
        ssize_t n = ::recv(m_fd, tmp, sizeof(tmp), 0);
        if (n > 0)
            m_recvBuf.append(tmp, static_cast<size_t>(n));
        else if (n == 0)
        {
            disconnect();
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            disconnect();
            return;
        }
    }

    HttpResponse rsp;
    size_t       consumed = 0;
    HttpParseResult pr = HttpParser::parseResponse(m_recvBuf.data(), m_recvBuf.size(),
                                                   consumed, rsp);
    if (pr == HttpParseResult::OK)
    {
        LOG_INFO("外呼客户端响应: status=%d reason=%s bodyLen=%zu preview=%.64s",
                 rsp.status, rsp.reason.c_str(), rsp.body.size(),
                 rsp.body.empty() ? "" : rsp.body.c_str());
        m_recvBuf.erase(0, consumed);
        disconnect();
    }
}

void GlobalHttpClient::poll(int timeoutMs)
{
    if (m_fd < 0 || m_epollFd < 0)
        return;
    epoll_event events[8];
    int n = ::epoll_wait(m_epollFd, events, 8, timeoutMs);
    for (int i = 0; i < n; ++i)
    {
        if (events[i].events & EPOLLOUT)
        {
            if (!m_connectNotified)
            {
                m_connectNotified = true;
                LOG_INFO("外呼客户端连接成功: %s:%u", m_cfg.host.c_str(), m_cfg.port);
            }
            trySend();
        }
        if (events[i].events & EPOLLIN)
            onReadable();
        if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
            disconnect();
    }
}

void GlobalHttpClient::tickReconnect(uint64_t nowMs)
{
    if (!isConfigured() || !m_cfg.reconnect)
        return;
    if (isConnected())
    {
        m_retryDelayMs = MIN_RETRY_MS;
        return;
    }
    if (nowMs < m_nextRetryMs)
        return;
    if (!doConnect())
        m_retryDelayMs = std::min(m_retryDelayMs * 2, MAX_RETRY_MS);
    m_nextRetryMs = nowMs + m_retryDelayMs;
}

bool GlobalHttpClient::sendGet(const char* path)
{
    if (!isConnected())
        return false;
    m_sendBuf = HttpCodec::buildGetRequest(path, m_cfg.host.c_str());
    trySend();
    return true;
}
