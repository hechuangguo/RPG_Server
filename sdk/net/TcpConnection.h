/**
 * @file    TcpConnection.h
 * @brief  单条 TCP/TLS 连接封装（ET 非阻塞，MsgHeader 拆包）
 */

#pragma once

#include "NetDefine.h"
#include "RingBuffer.h"

#include <array>

struct ssl_st;
typedef struct ssl_st SSL;

/**
 * @brief TLS 握手/应用数据阶段
 */
enum class TlsState : uint8_t
{
    None         = 0, /**< 明文 TCP */
    Handshaking  = 1, /**< TLS 握手中 */
    Established  = 2, /**< TLS 就绪，可收发应用数据 */
};

/**
 * @brief 单条 TCP 或 TLS 连接
 */
class TcpConnection
{
public:
    TcpConnection(int fd, ConnID id, INetCallback* cb, SSL* ssl = nullptr, bool serverSide = false);

    ~TcpConnection();

    ConnID GetID() const { return m_id; }
    int    GetFd() const { return m_fd; }
    bool   IsClosed() const { return m_closed; }

    /** @brief TLS 握手是否已完成（明文连接恒 true） */
    bool isTlsReady() const;

    /** @brief 是否仍处 TLS 握手 */
    bool isTlsHandshaking() const;

    /** @brief 握手完成后触发 OnConnect（幂等） */
    void tryFireConnect();

    /** @brief OnConnect 是否已触发 */
    bool connectFired() const { return m_connectFired; }

    bool SendMsg(uint8_t module, uint8_t sub, const char* data, uint16_t len);
    bool SendMsg(uint16_t flatMsgId, const char* data, uint16_t len);

    void OnReadable();
    void OnWritable();
    void Close();

    /** @brief 发送缓冲区是否仍有未写出的数据（TLS 可能需先读再写） */
    bool hasPendingSend() const { return m_sendBuf.ReadableBytes() > 0; }

private:
    bool driveTlsHandshake();
    void readPlain();
    void readTls();
    void writePlain();
    void writeTls();
    void processMessages();
    void freeSsl();

    int           m_fd;
    ConnID        m_id;
    INetCallback* m_cb;
    RingBuffer    m_recvBuf;
    RingBuffer    m_sendBuf;
    SSL*          m_ssl;
    TlsState      m_tlsState;
    bool          m_serverSide;
    bool          m_closed;
    bool          m_connectFired;
    bool          m_inReadHandler = false; /**< 正在 processMessages，禁止 SendMsg 同步写 */
    bool          m_tlsFailLogged = false; /**< 本连接已记录 TLS 握手失败 WARN，避免 Poll 重复刷 */
    std::array<char, MAX_PACKET_SIZE> m_msgBody; /**< 拆包临时缓冲，避免栈上大数组 */
};
