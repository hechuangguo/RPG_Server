/**
 * @file    MsgIngress.h
 * @brief  统一消息管道入口（收包分类后的校验/解包/分发）
 */

#pragma once

#include "../util/MsgDispatcher.h"
#include "../util/ClientMsgDispatcher.h"
#include "GwClientUnwrap.h"
#include "NetDefine.h"
#include <cstdint>

/** @brief 连接在管道中的语义分类 */
enum class ConnKind : uint8_t
{
    ClientWire = 0,      /**< 直连客户端（Gateway 玩家口、Login 客户端口） */
    InternalS2S = 1,     /**< 区内服 TCP */
    GwWrappedClient = 2, /**< 经 GW_CLIENT_MSG 透传（Scene/Session） */
};

/** @brief 单次消息入站上下文 */
struct IngressContext
{
    ConnKind kind;           /**< 连接语义 */
    ConnID connId;           /**< 当前 TCP 连接 ID */
    uint32_t clientConnId;   /**< GwWrapped 时网关侧客户端 conn */
    uint8_t module;          /**< 协议 module */
    uint8_t sub;             /**< 协议 sub */
    const char* data;        /**< 包体指针 */
    uint16_t len;            /**< 包体长度 */
};

/**
 * @brief 消息管道统一分发（L2 Ingress → L4 Dispatch）
 *
 * 区内未命中打 DEBUG；客户端未命中打 WARN。
 */
class MsgIngress
{
public:
    /**
     * @brief 区内 S2S 消息分发
     * @return 是否命中 handler
     */
    static bool dispatchInternal(ConnID connId, uint8_t module, uint8_t sub,
                                 const char* data, uint16_t len);

    /**
     * @brief 客户端 wire 消息分发
     * @param connId 客户端连接 ID（GwWrapped 时传 clientConnId）
     */
    static bool dispatchClient(uint32_t connId, uint8_t module, uint8_t sub,
                               const char* data, uint16_t len);

    /**
     * @brief 按 IngressContext 统一入口
     */
    static bool onMessage(const IngressContext& ctx);

    /**
     * @brief GW_CLIENT_MSG 解包后分发至 ClientMsgDispatcher
     * @param fromConn 网关入站连接（供回调记录）
     * @param onBeforeDispatch 解包后、分发前钩子（可选记录 gateway conn）
     */
    template<typename HookFn>
    static bool dispatchGwWrapped(ConnID fromConn, const char* data, uint16_t len,
                                  HookFn&& onBeforeDispatch)
    {
        auto unwrapped = unwrapGwClientMsg(data, len);
        if (!unwrapped)
            return false;
        onBeforeDispatch(fromConn, *unwrapped);
        return dispatchClient(unwrapped->clientConnId, unwrapped->module, unwrapped->sub,
                              unwrapped->body, unwrapped->bodyLen);
    }
};
