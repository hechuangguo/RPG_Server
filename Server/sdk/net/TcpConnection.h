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
 * 连接状态机：
 * - 状态通过 m_closed 布尔标记管理，仅有两种状态：Active（false）和 Closed（true）。
 * - 状态转换：
 *   - Active → Closed：由 Close() 触发（对端关闭、recv/send 错误、主动关闭）
 *   - Closed 是终态，不可逆。
 * - Close() 是幂等操作：重复调用安全（内部检查 m_closed 防止重复关闭 fd 和重复回调）。
 * - 生命周期由 shared_ptr 管理，TcpServer/TcpClient 持有 shared_ptr，
 *   Close() 后连接对象可能仍被引用，直到引用计数归零才析构。
 *
 * epoll 事件处理流程：
 * - EPOLLIN  → OnReadable()：
 *   1. 循环 recv() 直到返回 EAGAIN/EWOULDBLOCK（ET 模式必须读尽）
 *   2. 每次收到的数据写入 m_recvBuf（RingBuffer）
 *   3. recv() 返回 0 表示对端关闭 → 调用 Close()
 *   4. recv() 出错且非 EAGAIN → 调用 Close()
 *   5. 数据全部读完后调用 ProcessMessages() 尝试拆包
 *
 * - EPOLLOUT → OnWritable()：
 *   1. 循环从 m_sendBuf（RingBuffer）取出数据 send()
 *   2. send() 成功则 Consume 已发送字节数
 *   3. send() 返回 EAGAIN 表示发送缓冲区已满 → 退出循环等待下次 EPOLLOUT
 *   4. send() 出错且非 EAGAIN → 调用 Close()
 *
 * RingBuffer 与连接的交互：
 * - 接收缓冲区 m_recvBuf：
 *   - OnReadable() 写入（生产者），ProcessMessages() 读取（消费者）。
 *   - 支持半包处理：若缓冲区中数据不足一条完整消息（头部+消息体），
 *     数据保留在缓冲区，等待下次 OnReadable() 补齐。
 * - 发送缓冲区 m_sendBuf：
 *   - SendMsg() 写入（生产者），OnWritable() 读取并 send()（消费者）。
 *   - 若缓冲区满，SendMsg() 返回 false（背压机制，调用方需处理）。
 * - 两个缓冲区大小由 NetDefine.h 中的 RECV_BUFFER_SIZE / SEND_BUFFER_SIZE 决定。
 *
 * 使用方式：由 TcpServer / TcpClient 创建和管理生命周期（shared_ptr）。
 */

#pragma once
#include "NetDefine.h"
#include "MsgId.h"
#include "RingBuffer.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <cstring>

/**
 * @brief 单条 TCP 连接封装
 *
 * 封装非阻塞 socket fd，提供消息收发、拆包、连接关闭等功能。
 * 由 TcpServer（服务端 accept 后创建）或 TcpClient（客户端 connect 后创建）管理。
 */
class TcpConnection
{
public:
    /**
     * @brief 构造连接对象
     * @param fd 已 accept / connect 的 socket fd（必须为非阻塞模式）
     * @param id 全局唯一的连接 ID
     * @param cb 上层事件回调接口（生命周期由调用方管理）
     *
     * 初始化接收缓冲区（RECV_BUFFER_SIZE）和发送缓冲区（SEND_BUFFER_SIZE）。
     */
    TcpConnection(int fd, ConnID id, INetCallback* cb)
        : m_fd(fd), m_id(id), m_cb(cb)
        , m_recvBuf(RECV_BUFFER_SIZE)
        , m_sendBuf(SEND_BUFFER_SIZE)
        , m_closed(false)
    {}

    /** @brief 析构时自动关闭 socket（若尚未关闭） */
    ~TcpConnection() { Close(); }

    /** @brief 获取连接 ID */
    ConnID   GetID()   const { return m_id;   }

    /** @brief 获取底层 socket fd（关闭后返回 -1） */
    int      GetFd()   const { return m_fd;   }

    /** @brief 连接是否已关闭 */
    bool     IsClosed()const { return m_closed;}

    /**
     * @brief 发送一条消息（自动添加 MsgHeader）
     * @param module 功能模块号
     * @param sub    子消息号
     * @param data   消息体指针（可为 nullptr，此时 len 必须为 0）
     * @param len    消息体长度（字节）
     */
    bool SendMsg(uint8_t module, uint8_t sub, const char* data, uint16_t len)
    {
        if (m_closed) return false;
        if (len > MAX_PACKET_SIZE) return false;
        MsgHeader hdr{};
        hdr.bodyLen = len;
        hdr.module  = module;
        hdr.sub     = sub;
        if (!m_sendBuf.Write(reinterpret_cast<const char*>(&hdr), MSG_HEADER_SIZE))
            return false;
        if (len > 0 && data && !m_sendBuf.Write(data, len))
            return false;
        // ET 模式下 EPOLLOUT 仅在“不可写→可写”的边沿触发一次；连接建立时的可写
        // 边沿被消费后，之后缓冲的数据若不主动发送，会一直滞留到下一次边沿（例如
        // 定时器驱动的注册/心跳因此永远发不出去）。故此处缓冲后立即尝试 flush，
        // 未发尽的部分再由内核缓冲区满（EAGAIN）后的 EPOLLOUT 边沿继续驱动。
        if (!m_closed)
            OnWritable();
        return true;
    }

    /** @brief 使用扁平协议号发送（高字节 module，低字节 sub） */
    bool SendMsg(uint16_t flatMsgId, const char* data, uint16_t len)
    {
        return SendMsg(msgModule(flatMsgId), msgSub(flatMsgId), data, len);
    }

    /**
     * @brief epoll 返回 EPOLLIN 时调用（TcpServer/TcpClient 驱动）
     *
     * 接收流程（ET 模式必须循环读尽）：
     * 1. 循环调用 recv(fd, buf, 4096, 0)
     * 2. n > 0：将数据追加写入 m_recvBuf
     * 3. n == 0：对端正常关闭 → Close() 并返回
     * 4. n < 0 && (EAGAIN || EWOULDBLOCK)：无更多数据 → 退出循环
     * 5. n < 0 && 其他错误：异常关闭 → Close() 并返回
     * 6. 循环结束后调用 ProcessMessages() 尝试拆包
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
     * @brief epoll 返回 EPOLLOUT 时调用（TcpServer/TcpClient 驱动）
     *
     * 发送流程（ET 模式必须写尽）：
     * 1. 循环检查 m_sendBuf 是否有可读数据
     * 2. Peek 一块数据（最多 4096 字节）到临时缓冲区
     * 3. 调用 send(fd, buf, len, MSG_NOSIGNAL) 写出（MSG_NOSIGNAL 防止 SIGPIPE）
     * 4. n > 0：Consume 已发送字节数，继续循环
     * 5. n < 0 && (EAGAIN || EWOULDBLOCK)：内核发送缓冲区已满 → 退出等待下次 EPOLLOUT
     * 6. n < 0 && 其他错误：异常关闭 → Close() 并返回
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
     * @brief 关闭连接（幂等操作）
     *
     * 执行流程：
     * 1. 检查 m_closed 标志，已关闭则直接返回
     * 2. 设置 m_closed = true
     * 3. close(m_fd)，将 m_fd 置为 -1（防止后续误用）
     * 4. 触发回调 m_cb->OnDisconnect(m_id)（通知上层连接已断开）
     *
     * @note  回调在 Close() 调用栈内同步执行。调用方需确保回调中不依赖已关闭的资源。
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
     * 拆包算法（基于长度前缀协议）：
     * 1. Peek MSG_HEADER_SIZE 字节 → 解析 MsgHeader（bodyLen, module, sub）
     * 2. 计算完整消息长度 = MSG_HEADER_SIZE + hdr.bodyLen
     */
    void ProcessMessages()
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
            uint32_t total = MSG_HEADER_SIZE + hdr.bodyLen;
            if (m_recvBuf.ReadableBytes() < total)
                break;
            m_recvBuf.Consume(MSG_HEADER_SIZE);
            char body[MAX_PACKET_SIZE];
            if (hdr.bodyLen > 0)
                m_recvBuf.Read(body, hdr.bodyLen);
            if (m_cb)
            {
                m_cb->OnMessage(m_id, hdr.module, hdr.sub,
                                hdr.bodyLen > 0 ? body : nullptr, hdr.bodyLen);
            }
        }
    }
    int          m_fd;        /**< 非阻塞 socket 文件描述符（关闭后为 -1） */
    ConnID       m_id;        /**< 连接全局 ID */
    INetCallback* m_cb;       /**< 上层回调接口（不负责释放） */
    RingBuffer   m_recvBuf;   /**< 接收环形缓冲区（OnReadable 写入，ProcessMessages 读取） */
    RingBuffer   m_sendBuf;   /**< 发送环形缓冲区（SendMsg 写入，OnWritable 读取） */
    bool         m_closed;    /**< 关闭标记（防止重复 Close 和重复回调） */
};
