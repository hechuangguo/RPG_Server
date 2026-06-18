/**
 * @file    GwClientUnwrap.h
 * @brief  GW_CLIENT_MSG 解包辅助（Scene/Session 共用）
 */

#pragma once

#include "../../protocal/InternalMsg.h"
#include "../util/MsgDispatcher.h"
#include "NetDefine.h"
#include <cstdint>
#include <optional>
#include <utility>

/** @brief 解包后的客户端 wire 片段 */
struct UnwrappedClientMsg
{
    uint32_t clientConnId; /**< 网关侧客户端连接 ID */
    uint8_t module;        /**< 客户端 module */
    uint8_t sub;           /**< 客户端 sub */
    const char* body;      /**< 客户端包体指针 */
    uint16_t bodyLen;      /**< 客户端包体长度 */
};

/**
 * @brief 从 GW_CLIENT_MSG 载荷解出客户端 module/sub/body
 * @param data 完整区内包体（Msg_GW_ClientMsg + body）
 * @param len  包体总长度
 * @return 边界合法时返回 UnwrappedClientMsg；否则 nullopt
 */
inline std::optional<UnwrappedClientMsg> unwrapGwClientMsg(const char* data, uint16_t len)
{
    if (!data || len < sizeof(Msg_GW_ClientMsg))
        return std::nullopt;

    const auto* hdr = reinterpret_cast<const Msg_GW_ClientMsg*>(data);
    if (len < sizeof(Msg_GW_ClientMsg) + hdr->dataLen)
        return std::nullopt;

    UnwrappedClientMsg out{};
    out.clientConnId = hdr->clientConnID;
    out.module = hdr->module;
    out.sub = hdr->sub;
    out.body = data + sizeof(Msg_GW_ClientMsg);
    out.bodyLen = hdr->dataLen;
    return out;
}

/**
 * @brief 注册 GW_CLIENT_MSG 解包并分发至 ClientMsgDispatcher 的区内 handler
 * @param onUnwrapped 解包成功后的回调（可记录 gateway 入站 conn）
 */
template<typename Fn>
inline void registerGwClientUnwrapHandler(MsgDispatcher& d, Fn&& onUnwrapped)
{
    d.Register(static_cast<uint16_t>(InternalMsgID::GW_CLIENT_MSG),
               [fn = std::forward<Fn>(onUnwrapped)](uint32_t fromConn,
                                                     const char* data,
                                                     uint16_t len) {
                   auto unwrapped = unwrapGwClientMsg(data, len);
                   if (!unwrapped)
                       return;
                   fn(static_cast<ConnID>(fromConn), *unwrapped);
               });
}
