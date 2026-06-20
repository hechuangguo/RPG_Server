/**
 * @file    TcpConnection.cpp
 * @brief   TcpConnection 收发与 TLS 握手实现
 */

#include "TcpConnection.h"
#include "MsgId.h"
#include "../log/Logger.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>

namespace {

bool isWouldBlock(int err)
{
    return err == EAGAIN || err == EWOULDBLOCK;
}

} // namespace

TcpConnection::TcpConnection(int fd, ConnID id, INetCallback* cb, SSL* ssl, bool serverSide)
    : m_fd(fd)
    , m_id(id)
    , m_cb(cb)
    , m_recvBuf(RECV_BUFFER_SIZE)
    , m_sendBuf(SEND_BUFFER_SIZE)
    , m_ssl(ssl)
    , m_tlsState(ssl ? TlsState::Handshaking : TlsState::None)
    , m_serverSide(serverSide)
    , m_closed(false)
    , m_connectFired(false)
{
}

TcpConnection::~TcpConnection()
{
    Close();
}

bool TcpConnection::isTlsReady() const
{
    return m_tlsState == TlsState::None || m_tlsState == TlsState::Established;
}

bool TcpConnection::isTlsHandshaking() const
{
    return m_tlsState == TlsState::Handshaking;
}

void TcpConnection::tryFireConnect()
{
    if (m_connectFired || m_closed || !m_cb || !isTlsReady())
        return;
    m_connectFired = true;
    m_cb->OnConnect(m_id);
}

bool TcpConnection::driveTlsHandshake()
{
    if (!m_ssl || m_tlsState != TlsState::Handshaking)
        return true;

    const int ret = m_serverSide ? SSL_accept(m_ssl) : SSL_connect(m_ssl);
    if (ret == 1)
    {
        m_tlsState = TlsState::Established;
        return true;
    }

    const int err = SSL_get_error(m_ssl, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        return false;

    if (!m_tlsFailLogged)
    {
        m_tlsFailLogged = true;
        /** 出站重连时对端未就绪常见 SYSCALL 错误，降级 DEBUG 避免刷 WARN */
        const bool expectedClientRetry = !m_serverSide && err == SSL_ERROR_SYSCALL;
        if (expectedClientRetry)
        {
            LOG_DEBUG("TLS handshake failed: conn=%u side=client sslErr=%d (peer not ready?)",
                      m_id, err);
        }
        else
        {
            LOG_WARN("TLS handshake failed: conn=%u side=%s sslErr=%d",
                     m_id, m_serverSide ? "server" : "client", err);
            unsigned long sslCode = 0;
            while ((sslCode = ERR_get_error()) != 0)
            {
                char buf[256];
                ERR_error_string_n(sslCode, buf, sizeof(buf));
                LOG_WARN("TLS handshake detail: conn=%u %s", m_id, buf);
            }
        }
    }

    Close();
    return false;
}

bool TcpConnection::SendMsg(uint8_t module, uint8_t sub, const char* data, uint16_t len)
{
    if (m_closed || !isTlsReady())
        return false;
    if (len > MAX_PACKET_SIZE)
        return false;

    MsgHeader hdr{};
    hdr.bodyLen = len;
    hdr.module  = module;
    hdr.sub     = sub;
    if (!m_sendBuf.Write(reinterpret_cast<const char*>(&hdr), MSG_HEADER_SIZE))
        return false;
    if (len > 0 && data && !m_sendBuf.Write(data, len))
        return false;
    if (!m_closed && !m_inReadHandler)
        OnWritable();
    return true;
}

bool TcpConnection::SendMsg(uint16_t flatMsgId, const char* data, uint16_t len)
{
    return SendMsg(msgModule(flatMsgId), msgSub(flatMsgId), data, len);
}

void TcpConnection::readPlain()
{
    char tmp[4096];
    while (true)
    {
        const ssize_t n = ::recv(m_fd, tmp, sizeof(tmp), 0);
        if (n > 0)
        {
            m_recvBuf.Write(tmp, static_cast<uint32_t>(n));
        }
        else if (n == 0)
        {
            Close();
            return;
        }
        else
        {
            if (isWouldBlock(errno))
                break;
            Close();
            return;
        }
    }
}

void TcpConnection::readTls()
{
    char tmp[4096];
    while (true)
    {
        const int n = SSL_read(m_ssl, tmp, static_cast<int>(sizeof(tmp)));
        if (n > 0)
        {
            m_recvBuf.Write(tmp, static_cast<uint32_t>(n));
        }
        else if (n == 0)
        {
            Close();
            return;
        }
        else
        {
            const int err = SSL_get_error(m_ssl, n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                break;
            if (err == SSL_ERROR_ZERO_RETURN)
            {
                Close();
                return;
            }
            Close();
            return;
        }
    }
}

void TcpConnection::OnReadable()
{
    if (m_closed)
        return;

    if (m_ssl && m_tlsState == TlsState::Handshaking)
    {
        if (!driveTlsHandshake())
            return;
        tryFireConnect();
    }

    if (m_closed || !isTlsReady())
        return;

    if (m_ssl)
        readTls();
    else
        readPlain();

    if (!m_closed)
    {
        m_inReadHandler = true;
        processMessages();
        m_inReadHandler = false;
    }

    /** 读完成后刷新 handler 内入队的回复（避免 SSL_write 重入 read 栈） */
    if (!m_closed && m_sendBuf.ReadableBytes() > 0)
        OnWritable();
}

void TcpConnection::writePlain()
{
    while (m_sendBuf.ReadableBytes() > 0)
    {
        char tmp[4096];
        const uint32_t toSend = std::min(m_sendBuf.ReadableBytes(), static_cast<uint32_t>(sizeof(tmp)));
        m_sendBuf.Peek(tmp, toSend);
        const ssize_t n = ::send(m_fd, tmp, toSend, MSG_NOSIGNAL);
        if (n > 0)
        {
            m_sendBuf.Consume(static_cast<uint32_t>(n));
        }
        else
        {
            if (isWouldBlock(errno))
                break;
            Close();
            return;
        }
    }
}

void TcpConnection::writeTls()
{
    while (m_sendBuf.ReadableBytes() > 0)
    {
        char tmp[4096];
        const uint32_t toSend = std::min(m_sendBuf.ReadableBytes(), static_cast<uint32_t>(sizeof(tmp)));
        m_sendBuf.Peek(tmp, toSend);
        const int n = SSL_write(m_ssl, tmp, static_cast<int>(toSend));
        if (n > 0)
        {
            m_sendBuf.Consume(static_cast<uint32_t>(n));
        }
        else
        {
            const int err = SSL_get_error(m_ssl, n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                break;
            Close();
            return;
        }
    }
}

void TcpConnection::OnWritable()
{
    if (m_closed)
        return;

    if (m_ssl && m_tlsState == TlsState::Handshaking)
    {
        if (!driveTlsHandshake())
            return;
        tryFireConnect();
    }

    if (m_closed || !isTlsReady())
        return;

    if (m_ssl)
        writeTls();
    else
        writePlain();
}

void TcpConnection::freeSsl()
{
    if (m_ssl)
    {
        /** 仅 Established 做 graceful shutdown；握手中 SSL_free 即可释放会话状态 */
        if (m_tlsState == TlsState::Established)
            SSL_shutdown(m_ssl);
        SSL_free(m_ssl);
        m_ssl = nullptr;
    }
    m_tlsState = TlsState::None;
    m_tlsFailLogged = false;
}

void TcpConnection::Close()
{
    if (m_closed)
        return;

    m_closed = true;
    freeSsl();
    if (m_fd >= 0)
    {
        ::close(m_fd);
        m_fd = -1;
    }
    if (m_cb)
        m_cb->OnDisconnect(m_id);
}

void TcpConnection::processMessages()
{
    while (m_recvBuf.ReadableBytes() >= MSG_HEADER_SIZE)
    {
        MsgHeader hdr{};
        m_recvBuf.Peek(reinterpret_cast<char*>(&hdr), MSG_HEADER_SIZE);
        if (hdr.bodyLen > MAX_PACKET_SIZE)
        {
            Close();
            return;
        }
        const uint32_t total = MSG_HEADER_SIZE + hdr.bodyLen;
        if (m_recvBuf.ReadableBytes() < total)
            break;

        m_recvBuf.Consume(MSG_HEADER_SIZE);
        if (hdr.bodyLen > 0)
            m_recvBuf.Read(m_msgBody.data(), hdr.bodyLen);
        if (m_cb)
        {
            m_cb->OnMessage(m_id, hdr.module, hdr.sub,
                            hdr.bodyLen > 0 ? m_msgBody.data() : nullptr, hdr.bodyLen);
        }
    }
}
