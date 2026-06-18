/**
 * @file    SceneLoginMsg.cpp
 * @brief  SceneServer Login 区服指令（骨架）
 */

#include "SceneLoginMsg.h"
#include "SceneServer.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../sdk/log/Logger.h"
#include "../protocal/InternalMsg.h"

namespace
{
void onExternFwdRsp(SceneServer& /*server*/, ConnID /*fromConn*/,
                    const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SS_ExternForwardRsp))
        return;
    const auto* hdr = reinterpret_cast<const Msg_SS_ExternForwardRsp*>(data);
    LOG_DEBUG("场景服登录外联响应: inner=0x%04X seq=%u code=%d",
              hdr->innerMsgId, hdr->seq, hdr->code);
}

void onLoginGmCmd(ConnID fromConn, const char* data, uint16_t len)
{
    (void)fromConn;
    (void)data;
    LOG_DEBUG("场景服收到 LOGIN_GM_CMD_REQ: len=%u（骨架）", len);
}

void onLoginGmCmdWrapped(SceneServer& /*server*/, ConnID fromConn,
                         const char* data, uint16_t len)
{
    onLoginGmCmd(fromConn, data, len);
}
} // namespace

void SceneLoginMsgRegister(SceneServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalFree(d, server,
                         static_cast<uint16_t>(InternalMsgID::SS_EXTERN_FWD_RSP),
                         onExternFwdRsp);
    registerInternalFree(d, server,
                         static_cast<uint16_t>(InternalMsgID::LOGIN_GM_CMD_REQ),
                         onLoginGmCmdWrapped);
}
