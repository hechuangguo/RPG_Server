#pragma once
#include <cstdint>
#include <functional>
#include <string>

// ============================================================
//  网络基础定义
// ============================================================

constexpr int MAX_PACKET_SIZE    = 65535;
constexpr int RECV_BUFFER_SIZE   = 131072; // 128KB
constexpr int SEND_BUFFER_SIZE   = 131072;
constexpr int MAX_EPOLL_EVENTS   = 1024;
constexpr int LISTEN_BACKLOG     = 512;

// 连接标识
using ConnID = uint32_t;
constexpr ConnID INVALID_CONN_ID = 0;

// 消息头（固定4字节：2字节长度 + 2字节协议号）
#pragma pack(push, 1)
struct MsgHeader
{
    uint16_t length;   // 消息体长度（不含包头）
    uint16_t msgID;    // 协议号
};
#pragma pack(pop)

constexpr uint16_t MSG_HEADER_SIZE = sizeof(MsgHeader);

// 网络事件回调
struct INetCallback
{
    virtual ~INetCallback() = default;
    virtual void OnConnect(ConnID id)                                     = 0;
    virtual void OnDisconnect(ConnID id)                                  = 0;
    virtual void OnMessage(ConnID id, uint16_t msgID,
                           const char* data, uint16_t len)                = 0;
};
