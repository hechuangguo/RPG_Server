/**
 * @file    TcpConnection.h
 * @brief  单条 TCP 连接的封装（ET 非阻塞模式）
 *
 * 职责：
 * - 持有收发两个 RingBuffer
 * - 自动进行 MsgHeader 拆包（ProcessMessages）
 * - 通过 INetCallback 将完整消息抛给上层
 * - Close() 自动回调 OnDisconnect
 *
 * 使用方式：由 TcpServer / TcpClient 创建和管理生命周期（shared_ptr）。
 */

#pragma once
#include "NetDefine.h"
#include "RingBuffer.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <cstring>

class TcpConnection
{
public:
    /**
     * @brief 构造连接对象
     * @param fd 已 accept / connect 的 socket fd（非阻塞）
     * @param id 全局唯一的连接 ID
     * @param cb 上层事件回调
     */
    TcpConnection(int fd, ConnID id, INetCallback* cb)
        : m_fd(fd), m_id(id), m_cb(cb)
        , m_recvBuf(RECV_BUFFER_SIZE)
        , m_sendBuf(SEND_BUFFER_SIZE)
        , m_closed(false)
    {}

    /** @brief 析构时自动关闭 socket */
    ~TcpConnection() { Close(); }

    /** @brief 获取连接 ID */
    ConnID   GetID()   const { return m_id;   }

    /** @brief 获取底层 socket fd */
    int      GetFd()   const { return m_fd;   }

    /** @brief 连接是否已关闭 */
    bool     IsClosed()const { return m_closed;}

    /**
     * @brief 发送一条消息（自动添加 MsgHeader）
     * @param msgID 协议号
     * @param data  消息体指针
     * @param len   消息体长度
     * @return 写入发送缓冲区成功返回 true
     * @note   实际数据由后续 OnWritable() 异步写出
     */
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

    /**
     * @brief epoll 返回 EPOLLIN 时调用
     *
     * 循环 recv() 直到 EAGAIN，然后调用 ProcessMessages() 拆包。
     */
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
                Close();  /**< 对端正常关闭 */
                return;
            }
            else
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break; /**< 无更多数据 */
                Close();
                return;
            }
        }
        ProcessMessages();
    }

    /**
     * @brief epoll 返回 EPOLLOUT 时调用
     *
     * 循环 send() 直到发送缓冲区为空或 EAGAIN。
     */
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

    /**
     * @brief 关闭连接
     *
     * 幂等操作：关闭 socket fd，回调 OnDisconnect。
     */
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
    /**
     * @brief 从接收缓冲区中解析完整消息
     *
     * 算法：Peek 消息头 → 检查剩余字节是否足够 → Consume 消息头 + Read 消息体 → 回调 OnMessage。
     * 循环直到不足一条完整消息为止，半包数据留在缓冲区等待下次补齐。
     */
    void ProcessMessages()
    {
        while (m_recvBuf.ReadableBytes() >= MSG_HEADER_SIZE)
        {
            MsgHeader hdr;
            m_recvBuf.Peek(reinterpret_cast<char*>(&hdr), MSG_HEADER_SIZE);
            uint32_t total = MSG_HEADER_SIZE + hdr.length;
            if (m_recvBuf.ReadableBytes() < total) break;  /**< 半包，等待更多数据 */
            m_recvBuf.Consume(MSG_HEADER_SIZE);
            char body[MAX_PACKET_SIZE];
            if (hdr.length > 0)
                m_recvBuf.Read(body, hdr.length);
            if (m_cb)
                m_cb->OnMessage(m_id, hdr.msgID, hdr.length > 0 ? body : nullptr, hdr.length);
        }
    }

    int          m_fd;        /**< 非阻塞 socket 文件描述符 */
    ConnID       m_id;        /**< 连接全局 ID */
    INetCallback* m_cb;       /**< 上层回调接口（不负责释放） */
    RingBuffer   m_recvBuf;   /**< 接收环形缓冲区 */
    RingBuffer   m_sendBuf;   /**< 发送环形缓冲区 */
    bool         m_closed;    /**< 关闭标记（防止重复 Close） */
};
