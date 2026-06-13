/**
 * @file    SessionLoginMsg.cpp
 * @brief  SessionServer Login 区服指令（骨架）
 */

#include "SessionLoginMsg.h"
#include "SessionServer.h"
#include "../sdk/log/Logger.h"
#include "../protocal/InternalMsg.h"

namespace
{
void onExternFwdRsp(SessionServer& /*server*/, ConnID /*fromConn*/,
                      const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SS_ExternForwardRsp))
        return;
    const auto* hdr = reinterpret_cast<const Msg_SS_ExternForwardRsp*>(data);
    LOG_DEBUG("SessionLogin: SS_EXTERN_FWD_RSP inner=0x%04X seq=%u code=%d",
              hdr->innerMsgId, hdr->seq, hdr->code);
}

void onLoginRecharge(ConnID fromConn, const char* data, uint16_t len)
{
    (void)fromConn;
    (void)data;
    LOG_DEBUG("SessionLogin: LOGIN_RECHARGE_REQ len=%u (skeleton)", len);
}
} // namespace

void SessionLoginMsgRegister(SessionServer& server)
{
    auto& d = MsgDispatcher::Instance();
    d.Register(static_cast<uint16_t>(InternalMsgID::SS_EXTERN_FWD_RSP),
               [&server](uint32_t c, const char* data, uint16_t l) {
                   onExternFwdRsp(server, c, data, l);
               });
    d.Register(static_cast<uint16_t>(InternalMsgID::LOGIN_RECHARGE_REQ),
               [](uint32_t c, const char* data, uint16_t l) {
                   onLoginRecharge(c, data, l);
               });
}
