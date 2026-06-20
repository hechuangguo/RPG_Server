/**
 * @file    ClientWireSend.h
 * @brief  客户端 Protobuf wire 消息发送辅助
 */

#pragma once

#include "ClientProtoWire.h"
#include "NetDefine.h"

/**
 * @brief 序列化 Protobuf 并发送客户端消息
 * @tparam Sender 须实现 SendMsg(ConnID, uint8_t, uint8_t, const char*, uint16_t)
 */
template<typename ProtoMsg, typename Sender>
inline bool sendClientProto(Sender& sender, ConnID conn, uint8_t module, uint8_t sub,
                            const ProtoMsg& msg)
{
    std::string body;
    if (!serializeProto(msg, body))
        return false;
    return sender.SendMsg(conn, module, sub, body.data(),
                          static_cast<uint16_t>(body.size()));
}

/**
 * @brief 按指定 module/sub 发送 Protobuf 客户端消息（Gateway/Login 共用）
 * @tparam ProtoMsg Protobuf message 类型
 * @tparam Sender   须实现 SendMsg
 */
template<typename ProtoMsg, typename Sender>
inline bool sendClientProtoModule(Sender& sender, ConnID conn, uint8_t module, uint8_t sub,
                                  const ProtoMsg& msg)
{
    return sendClientProto(sender, conn, module, sub, msg);
}
