/**
 * @file    MsgIngress.cpp
 * @brief  消息管道统一分发实现
 */

#include "MsgIngress.h"
#include "../log/Logger.h"

bool MsgIngress::dispatchInternal(ConnID connId, uint8_t module, uint8_t sub,
                                  const char* data, uint16_t len)
{
    if (MsgDispatcher::Instance().Dispatch(connId, module, sub, data, len))
        return true;
    LOG_DEBUG("区内消息未注册: conn=%u mod=0x%02X sub=0x%02X len=%u",
              connId, module, sub, len);
    return false;
}

bool MsgIngress::dispatchClient(uint32_t connId, uint8_t module, uint8_t sub,
                                const char* data, uint16_t len)
{
    if (ClientMsgDispatcher::Instance().Dispatch(connId, module, sub, data, len))
        return true;
    LOG_WARN("客户端消息未注册: conn=%u mod=0x%02X sub=0x%02X len=%u",
             connId, module, sub, len);
    return false;
}

bool MsgIngress::onMessage(const IngressContext& ctx)
{
    switch (ctx.kind)
    {
    case ConnKind::ClientWire:
        return dispatchClient(ctx.connId, ctx.module, ctx.sub, ctx.data, ctx.len);
    case ConnKind::InternalS2S:
        return dispatchInternal(ctx.connId, ctx.module, ctx.sub, ctx.data, ctx.len);
    case ConnKind::GwWrappedClient:
        return dispatchClient(ctx.clientConnId, ctx.module, ctx.sub, ctx.data, ctx.len);
    }
    return false;
}
