/**
 * @file    GameZoneMsgDispatch.cpp
 * @brief  独立服游戏区消息解包分发
 */

#include "GameZoneMsgDispatch.h"
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

    if (!MsgDispatcher::Instance().Dispatch(fromConn, hdr->innerMsgId, body, hdr->dataLen))
    {
        LOG_DEBUG("GameZoneFwd: unhandled inner=0x%04X len=%u",
                  hdr->innerMsgId, hdr->dataLen);
    }
}
