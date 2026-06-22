/**
 * @file    GameZoneMsgDispatch.cpp
 * @brief  独立服游戏区消息解包分发
 */

#include "GameZoneMsgDispatch.h"
#include "GameZoneReply.h"
#include "MsgDispatcher.h"
#include "../../protocal/InternalMsg.h"
#include "../log/Logger.h"

void GameZoneMsgRegisterForwardDispatch()
{
    MsgDispatcher::Instance().Register(
        static_cast<uint16_t>(InternalMsgID::EXT_GAMEZONE_FWD_REQ),
        [](uint32_t c, const char* data, uint16_t len) {
            GameZoneOnForwardReq(c, data, len);
        });
}

void GameZoneOnForwardReq(ConnID fromConn, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SS_ExternForward))
        return;

    const auto* hdr = reinterpret_cast<const Msg_SS_ExternForward*>(data);
    const char* body = data + sizeof(Msg_SS_ExternForward);
    if (len < sizeof(Msg_SS_ExternForward) + hdr->dataLen)
        return;

    LOG_DEBUG("GameZoneFwd: 收到转发 inner=0x%04X seq=%u len=%u conn=%u",
              hdr->innerMsgId, hdr->seq, hdr->dataLen, fromConn);

    gameZonePushForwardContext(fromConn, *hdr);
    const GameZoneForwardContext* ctx = gameZonePeekForwardContext(fromConn, hdr->seq);
    gameZoneSetCurrentForwardContext(fromConn, ctx);
    const bool dispatched =
        MsgDispatcher::Instance().Dispatch(fromConn, hdr->innerMsgId, body, hdr->dataLen);
    gameZoneSetCurrentForwardContext(INVALID_CONN_ID, nullptr);
    if (!dispatched)
    {
        LOG_DEBUG("GameZoneFwd: unhandled inner=0x%04X len=%u",
                  hdr->innerMsgId, hdr->dataLen);
        gameZonePopForwardContext(fromConn, hdr->seq);
        return;
    }
    if (hdr->seq != 0 && gameZonePeekForwardContext(fromConn, hdr->seq) != nullptr)
    {
        LOG_WARN("GameZoneFwd: handler 未对称回包 inner=0x%04X seq=%u conn=%u",
                 hdr->innerMsgId, hdr->seq, fromConn);
        gameZonePopForwardContext(fromConn, hdr->seq);
    }
}
