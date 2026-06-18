/**
 * @file    ClientWireSend.h
 * @brief  客户端 wire 消息发送辅助（initClientMsg + SendMsg 合一）
 */

#pragma once

#include "../../Common/ClientMsgBody.h"
#include "NetDefine.h"

/**
 * @brief 初始化 wire 前缀并发送定长客户端消息
 * @tparam MsgT 须含 kModule、kSub 及 module、sub 成员
 * @tparam Sender 须实现 SendMsg(ConnID, uint8_t, uint8_t, const char*, uint16_t)
 */
template<typename MsgT, typename Sender>
inline bool sendClientWire(Sender& sender, ConnID conn, MsgT& msg)
{
    initClientMsg(msg);
    return sender.SendMsg(conn, MsgT::kModule, MsgT::kSub,
                          reinterpret_cast<const char*>(&msg), sizeof(msg));
}
