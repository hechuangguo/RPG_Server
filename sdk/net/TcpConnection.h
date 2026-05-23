#pragma once
#include "NetDefine.h"
#include "RingBuffer.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <cstring>

// ============================================================
//  TCP 连接封装
// ============================================================
class TcpConnection
{
public:
    TcpConnection(int fd, ConnID id, INetCallback* cb)
        : m_fd(fd), m_id(id), m_cb(cb)
        , m_recvBuf(RECV_BUFFER_SIZE)
        , m_sendBuf(SEND_BUFFER_SIZE)
        , m_closed(false)
    {}
    ~TcpConnection() { Close(); }

    ConnID   GetID()   const { return m_id;   }
    int      GetFd()   const { return m_fd;   }
    bool     IsClosed()const { return m_closed;}

    // 发送消息（自动添加包头）
    bool SendMsg(uint16_t msgID, const char* data, uint16_t len)
    {
        if (m_closed) return false;
        MsgHeader hdr;
        hdr.length = len;
        hdr.msgID  = msgID;
        if (!m_sendBuf.Write(reinterpret_cast<const char*>(&hdr), MSG_HEADER_SIZE))
            return false;
        if (len > 0 && data)
            return m_sendBuf.Write(data, len);
        return true;
    }

    // epoll 可读时调用
    void OnReadable()
    {
        char tmp[4096];
        while (true)
        {
            ssize_t n = ::recv(m_fd, tmp, sizeof(tmp), 0);
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
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                Close();
                return;
            }
        }
        // 解析完整消息
        ProcessMessages();
    }

    // epoll 可写时调用
    void OnWritable()
    {
        while (m_sendBuf.ReadableBytes() > 0)
        {
            char tmp[4096];
            uint32_t toSend = std::min(m_sendBuf.ReadableBytes(), (uint32_t)sizeof(tmp));
            m_sendBuf.Peek(tmp, toSend);
            ssize_t n = ::send(m_fd, tmp, toSend, MSG_NOSIGNAL);
            if (n > 0)
            {
                m_sendBuf.Consume(static_cast<uint32_t>(n));
            }
            else
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                Close();
                return;
            }
        }
    }

    void Close()
    {
        if (!m_closed)
        {
            m_closed = true;
            ::close(m_fd);
            m_fd = -1;
            if (m_cb) m_cb->OnDisconnect(m_id);
        }
    }

private:
    void ProcessMessages()
    {
        while (m_recvBuf.ReadableBytes() >= MSG_HEADER_SIZE)
        {
            MsgHeader hdr;
            m_recvBuf.Peek(reinterpret_cast<char*>(&hdr), MSG_HEADER_SIZE);
            uint32_t total = MSG_HEADER_SIZE + hdr.length;
            if (m_recvBuf.ReadableBytes() < total) break;
            m_recvBuf.Consume(MSG_HEADER_SIZE);
            char body[MAX_PACKET_SIZE];
            if (hdr.length > 0)
                m_recvBuf.Read(body, hdr.length);
            if (m_cb)
                m_cb->OnMessage(m_id, hdr.msgID, hdr.length > 0 ? body : nullptr, hdr.length);
        }
    }

    int          m_fd;
    ConnID       m_id;
    INetCallback* m_cb;
    RingBuffer   m_recvBuf;
    RingBuffer   m_sendBuf;
    bool         m_closed;
};
