/**
 * @file    GameZoneReply.cpp
 * @brief   外联服 EXT_GAMEZONE_FWD 对称回包实现
 */

#include "GameZoneReply.h"

#include "../net/TcpServer.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace
{

using ForwardContextKey = uint64_t;

ForwardContextKey makeForwardContextKey(ConnID superConn, uint32_t seq)
{
    return (static_cast<uint64_t>(superConn) << 32) | static_cast<uint64_t>(seq);
}

std::unordered_map<ForwardContextKey, GameZoneForwardContext> g_forwardContextByKey;

ConnID g_activeSuperConn = INVALID_CONN_ID;
const GameZoneForwardContext* g_activeForwardContext = nullptr;

} // namespace

void gameZonePushForwardContext(ConnID superConn, const Msg_SS_ExternForward& reqHdr)
{
    if (reqHdr.seq == 0)
        return;

    GameZoneForwardContext ctx{};
    ctx.reqHdr = reqHdr;
    g_forwardContextByKey[makeForwardContextKey(superConn, reqHdr.seq)] = ctx;
}

void gameZonePopForwardContext(ConnID superConn, uint32_t seq)
{
    if (seq == 0)
        return;
    g_forwardContextByKey.erase(makeForwardContextKey(superConn, seq));
}

const GameZoneForwardContext* gameZonePeekForwardContext(ConnID superConn, uint32_t seq)
{
    if (seq == 0)
        return nullptr;

    const auto it = g_forwardContextByKey.find(makeForwardContextKey(superConn, seq));
    if (it == g_forwardContextByKey.end())
        return nullptr;
    return &it->second;
}

void gameZoneSetCurrentForwardContext(ConnID superConn, const GameZoneForwardContext* ctx)
{
    g_activeSuperConn = superConn;
    g_activeForwardContext = ctx;
}

const GameZoneForwardContext* gameZoneCurrentForwardContext(ConnID superConn)
{
    if (superConn == INVALID_CONN_ID || superConn != g_activeSuperConn || !g_activeForwardContext)
        return nullptr;
    return g_activeForwardContext;
}

bool gameZoneSendForwardRsp(TcpServer& registerServer, ConnID superConn,
                            const Msg_SS_ExternForward& reqHdr, int32_t code,
                            const void* innerBody, uint16_t innerLen)
{
    Msg_SS_ExternForwardRsp rspHdr{};
    fillExternForwardRspFromReq(rspHdr, reqHdr, code, innerLen);

    std::vector<char> buf(sizeof(Msg_SS_ExternForwardRsp) + innerLen);
    std::memcpy(buf.data(), &rspHdr, sizeof(Msg_SS_ExternForwardRsp));
    if (innerLen > 0 && innerBody)
        std::memcpy(buf.data() + sizeof(Msg_SS_ExternForwardRsp), innerBody, innerLen);

    const bool sent = registerServer.SendMsg(
        superConn, static_cast<uint16_t>(InternalMsgID::EXT_GAMEZONE_FWD_RSP),
        buf.data(), static_cast<uint16_t>(buf.size()));
    if (reqHdr.seq != 0)
        gameZonePopForwardContext(superConn, reqHdr.seq);
    return sent;
}
