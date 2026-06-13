/**
 * @file    SuperExternRouter.cpp
 * @brief  SuperServer 外联转发实现
 */

#include "SuperExternRouter.h"
#include "SuperServer.h"
#include "../sdk/util/ExternalServerHub.h"
#include "../sdk/log/Logger.h"

#include <cstring>
#include <vector>

namespace
{
SubServerType toSubServerType(uint8_t v)
{
    return static_cast<SubServerType>(v);
}
} // namespace

void SuperExternMsgRegister(SuperServer& super)
{
    auto& d = MsgDispatcher::Instance();
    d.Register(static_cast<uint16_t>(InternalMsgID::SS_EXTERN_FWD_REQ),
               [&super](uint32_t c, const char* data, uint16_t len) {
                   SuperExternOnForwardReq(super, c, data, len);
               });
    d.Register(static_cast<uint16_t>(InternalMsgID::EXT_GAMEZONE_FWD_RSP),
               [&super](uint32_t c, const char* data, uint16_t len) {
                   SuperExternOnForwardRsp(super, c, data, len);
               });
}

bool SuperExternSendToExtern(SuperServer& super, SubServerType targetType,
                             const Msg_SS_ExternForward& hdr, const char* body)
{
    TcpClient* client = super.externHub().client(targetType);
    if (!client || !client->IsConnected())
    {
        LOG_WARN("SuperExtern: extern %u not connected", static_cast<unsigned>(targetType));
        return false;
    }

    std::vector<char> buf(sizeof(Msg_SS_ExternForward) + hdr.dataLen);
    std::memcpy(buf.data(), &hdr, sizeof(Msg_SS_ExternForward));
    if (hdr.dataLen > 0 && body)
        std::memcpy(buf.data() + sizeof(Msg_SS_ExternForward), body, hdr.dataLen);

    client->SendMsg(static_cast<uint16_t>(InternalMsgID::EXT_GAMEZONE_FWD_REQ),
                    buf.data(), static_cast<uint16_t>(buf.size()));
    return true;
}

bool SuperExternSendRspToGameZone(SuperServer& super, ConnID gameZoneConn,
                                  const Msg_SS_ExternForwardRsp& hdr,
                                  const char* body)
{
    if (gameZoneConn == INVALID_CONN_ID)
        return false;

    std::vector<char> buf(sizeof(Msg_SS_ExternForwardRsp) + hdr.dataLen);
    std::memcpy(buf.data(), &hdr, sizeof(Msg_SS_ExternForwardRsp));
    if (hdr.dataLen > 0 && body)
        std::memcpy(buf.data() + sizeof(Msg_SS_ExternForwardRsp), body, hdr.dataLen);

    super.tcpServer().SendMsg(gameZoneConn,
                              static_cast<uint16_t>(InternalMsgID::SS_EXTERN_FWD_RSP),
                              buf.data(), static_cast<uint16_t>(buf.size()));
    return true;
}

void SuperExternOnForwardReq(SuperServer& super, ConnID fromConn,
                             const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SS_ExternForward))
        return;

    const auto* hdr = reinterpret_cast<const Msg_SS_ExternForward*>(data);
    const char* body = data + sizeof(Msg_SS_ExternForward);
    if (len < sizeof(Msg_SS_ExternForward) + hdr->dataLen)
        return;

    const SubServerType target = toSubServerType(hdr->targetServerType);
    if (target != SubServerType::LOGIN && target != SubServerType::LOGGER &&
        target != SubServerType::GLOBAL && target != SubServerType::ZONE)
    {
        LOG_WARN("SuperExtern: invalid target type %u", hdr->targetServerType);
        return;
    }

    if (!SuperExternSendToExtern(super, target, *hdr, body))
    {
        if (hdr->seq != 0)
        {
            Msg_SS_ExternForwardRsp rsp{};
            rsp.sourceServerType = hdr->sourceServerType;
            rsp.sourceServerId   = hdr->sourceServerId;
            rsp.targetServerType = hdr->targetServerType;
            rsp.innerMsgId       = hdr->innerMsgId;
            rsp.seq              = hdr->seq;
            rsp.code             = -1;
            rsp.dataLen          = 0;
            SuperExternSendRspToGameZone(super, fromConn, rsp, nullptr);
        }
    }
    else
    {
        LOG_DEBUG("SuperExtern: fwd to extern type=%u inner=0x%04X len=%u fromConn=%u",
                  hdr->targetServerType, hdr->innerMsgId, hdr->dataLen, fromConn);
    }
}

void SuperExternOnForwardRsp(SuperServer& super, ConnID /*fromExternConn*/,
                             const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SS_ExternForwardRsp))
        return;

    const auto* hdr = reinterpret_cast<const Msg_SS_ExternForwardRsp*>(data);
    const char* body = data + sizeof(Msg_SS_ExternForwardRsp);
    if (len < sizeof(Msg_SS_ExternForwardRsp) + hdr->dataLen)
        return;

    ConnID targetConn = super.findSubServerConn(toSubServerType(hdr->sourceServerType));
    if (targetConn == INVALID_CONN_ID)
    {
        LOG_WARN("SuperExtern: rsp target type=%u offline", hdr->sourceServerType);
        return;
    }

    SuperExternSendRspToGameZone(super, targetConn, *hdr, body);
}
