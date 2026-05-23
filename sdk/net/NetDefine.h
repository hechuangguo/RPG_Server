/**
 * @file    NetDefine.h
 * @brief   网络基础定义 —— 常量、消息头结构、回调接口
 *
 * 所有网络模块（TcpServer / TcpClient / TcpConnection）共享此文件中的
 * 缓冲区大小、连接标识类型、消息封包格式以及异步回调抽象。
 */

#pragma once
#include <cstdint>
#include <functional>
#include <string>

// ============================================================
//  常量定义
// ============================================================

/** @brief 单个消息体的最大字节数 */
constexpr int MAX_PACKET_SIZE    = 65535;
/** @brief 接收环形缓冲区容量（128 KB） */
constexpr int RECV_BUFFER_SIZE   = 131072;
/** @brief 发送环形缓冲区容量（128 KB） */
constexpr int SEND_BUFFER_SIZE   = 131072;
/** @brief epoll_wait 单次最大事件数 */
constexpr int MAX_EPOLL_EVENTS   = 1024;
/** @brief listen() 积压队列长度 */
constexpr int LISTEN_BACKLOG     = 512;

// ============================================================
//  连接标识
// ============================================================

/** @brief 连接 ID 类型（全局唯一） */
using ConnID = uint32_t;
/** @brief 无效连接标识 */
constexpr ConnID INVALID_CONN_ID = 0;

// ============================================================
//  消息封包结构
// ============================================================

#pragma pack(push, 1)

/**
 * @brief 二进制消息头（定长 4 字节）
 *
 * 网络层自动追加/解析此头部，上层只需关心 msgID 与消息体。
 * length 字段仅包含消息体长度，不含消息头自身。
 */
struct MsgHeader
{
    uint16_t length;   /**< 消息体长度（不含本头部的 4 字节） */
    uint16_t msgID;    /**< 协议号，对应 ClientMsgID 或 InternalMsgID */
};

#pragma pack(pop)

/** @brief 消息头固定字节数（= sizeof(MsgHeader)） */
constexpr uint16_t MSG_HEADER_SIZE = sizeof(MsgHeader);

// ============================================================
//  网络事件回调（纯虚接口）
// ============================================================

/**
 * @brief 网络层异步事件回调接口
 *
 * TcpServer / TcpClient 通过此接口将连接事件与消息派发给上层逻辑。
 * 上层服务器类（如 SuperServer）实现此接口即可接收网络事件。
 */
struct INetCallback
{
    virtual ~INetCallback() = default;

    /** @brief 新连接建立后触发 */
    virtual void OnConnect(ConnID id)                                     = 0;

    /** @brief 连接断开后触发（已自动关闭 fd） */
    virtual void OnDisconnect(ConnID id)                                  = 0;

    /**
     * @brief 收到一条完整消息
     * @param id    来源连接 ID
     * @param msgID 协议号
     * @param data  消息体指针（可能为 nullptr）
     * @param len   消息体长度
     */
    virtual void OnMessage(ConnID id, uint16_t msgID,
                           const char* data, uint16_t len)                = 0;
};
