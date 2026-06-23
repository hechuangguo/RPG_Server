/**
 * @file    LoginExternOutbox.cpp
 * @brief   SuperServer → Login 外联出站串行队列实现
 */

#include "LoginExternOutbox.h"
#include "SuperServer.h"
#include "../sdk/util/ExternalServerHub.h"
#include "../sdk/util/LoginFlowTimeouts.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"

#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

namespace
{

enum class OutboxKind : uint8_t
{
    VerifyToken = 0,
    GatewayRegister,
    GatewayHeartbeat,
    UpdateLastUser,
    ZoneStatusReport,
    ExternGameZoneFwd,
};

struct OutboxItem
{
    OutboxKind kind = OutboxKind::VerifyToken;
    uint64_t enqueuedAtMs = 0;
    ConnID recordConn = INVALID_CONN_ID;
    ConnID gatewayWrapConn = INVALID_CONN_ID;
    Msg_Login_VerifyTokenReq verifyReq{};
    Msg_Login_GatewayRegister gatewayBody{};
    Msg_Login_ZoneStatusReport zoneReport{};
    std::vector<char> rawBody;
    bool sendStarted = false; /**< 已调用 SendMsg，待发缓冲区尚未清空 */
};

struct PendingVerifyForward
{
    ConnID recordConn = INVALID_CONN_ID; /**< Record 在 Super 侧的 conn */
    Msg_Login_VerifyTokenReq verifyReq{}; /**< 原始校验请求（断线重发用） */
    uint64_t sentAtMs = 0;               /**< 末次成功写出时刻 */
};

std::deque<OutboxItem> g_outbox;
std::unordered_map<uint32_t, PendingVerifyForward> g_recordConnByVerifySeq;
uint64_t g_loginConnReadyMs = 0;

constexpr size_t MAX_OUTBOX_SIZE = 256;
constexpr size_t MAX_FLUSH_PER_TICK = 1;

void sendVerifyTokenFailToRecord(SuperServer& super, ConnID recordConn,
                                 const Msg_Login_VerifyTokenReq& req, const char* reason)
{
    LOG_WARN("登录外联: 票据校验转发失败 seq=%u reason=%s", req.requestSeq, reason);
    Msg_Login_VerifyTokenRsp failRsp{};
    failRsp.requestSeq = req.requestSeq;
    failRsp.code = 1;
    failRsp.accid = 0;
    super.tcpServer().SendMsg(recordConn,
                              static_cast<uint16_t>(InternalMsgID::REC_VERIFY_TOKEN_RSP),
                              reinterpret_cast<const char*>(&failRsp), sizeof(failRsp));
}

void failAllPendingVerify(SuperServer& super, const char* reason)
{
    if (g_recordConnByVerifySeq.empty())
        return;
    LOG_WARN("登录外联: %s pending=%zu", reason, g_recordConnByVerifySeq.size());
    for (const auto& [seq, pending] : g_recordConnByVerifySeq)
    {
        (void)seq;
        sendVerifyTokenFailToRecord(super, pending.recordConn, pending.verifyReq, reason);
    }
    g_recordConnByVerifySeq.clear();
}

bool isVerifyItem(const OutboxItem& item)
{
    return item.kind == OutboxKind::VerifyToken;
}

/** @brief 外联 TLS 闪断时把在途校验退回队列，等待重连后重发（勿立即失败 Record） */
void requeueInFlightVerifyOnDisconnect(SuperServer& super)
{
    if (g_recordConnByVerifySeq.empty())
        return;
    for (const auto& [seq, pending] : g_recordConnByVerifySeq)
    {
        if (g_outbox.size() >= MAX_OUTBOX_SIZE)
        {
            sendVerifyTokenFailToRecord(super, pending.recordConn, pending.verifyReq,
                                        "登录外联出站队列已满");
            LOG_WARN("登录外联: 出站队列已满，无法重排队 seq=%u", seq);
            continue;
        }
        OutboxItem item{};
        item.kind = OutboxKind::VerifyToken;
        item.enqueuedAtMs = TimerMgr::NowMs();
        item.recordConn = pending.recordConn;
        item.verifyReq = pending.verifyReq;
        item.sendStarted = false;
        g_outbox.push_front(item);
        LOG_WARN("登录外联: 外联断开，票据校验重排队 seq=%u", seq);
    }
    g_recordConnByVerifySeq.clear();
}

/** @brief 外联断开时重置队列内校验项的发送状态，便于重连后重发 */
void resetQueuedVerifySendState()
{
    for (OutboxItem& item : g_outbox)
    {
        if (isVerifyItem(item))
            item.sendStarted = false;
    }
}

void replyGatewayWrapFailed(SuperServer& super, ConnID gatewayWrapConn, uint32_t gatewayServerId,
                            const char* reason)
{
    LOG_WARN("登录外联: %s id=%u", reason, gatewayServerId);
    Msg_SS_LoginGatewayWrapRsp rsp{};
    rsp.gatewayConnID = gatewayWrapConn;
    rsp.body.code = -1;
    rsp.body.gatewayServerId = gatewayServerId;
    super.tcpServer().SendMsg(gatewayWrapConn,
                              static_cast<uint16_t>(InternalMsgID::SS_LOGIN_GATEWAY_WRAP_RSP),
                              reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

bool canSendVerifyNow()
{
    return g_loginConnReadyMs != 0 &&
           TimerMgr::NowMs() - g_loginConnReadyMs >= LOGIN_CONN_WARMUP_MS;
}

/** @brief 票据校验已登记在途或 TLS 发送缓冲区仍有待发数据 */
bool isVerifyTransportBusy()
{
    if (!g_recordConnByVerifySeq.empty())
        return true;
    for (const OutboxItem& item : g_outbox)
    {
        if (isVerifyItem(item) && item.sendStarted)
            return true;
    }
    return false;
}

bool trySendItem(SuperServer& super, OutboxItem& item)
{
    TcpClient* login = super.externHub().client(SubServerType::LOGIN);
    if (!login || !login->canSend())
        return false;

    switch (item.kind)
    {
    case OutboxKind::VerifyToken:
    {
        if (!canSendVerifyNow())
            return false;
        if (!item.sendStarted)
        {
            if (!login->SendMsg(static_cast<uint16_t>(InternalMsgID::LOGIN_VERIFY_TOKEN_REQ),
                                reinterpret_cast<const char*>(&item.verifyReq),
                                sizeof(item.verifyReq)))
            {
                return false;
            }
            item.sendStarted = true;
        }
        if (login->hasPendingSend())
            return false;
        PendingVerifyForward pending{};
        pending.recordConn = item.recordConn;
        pending.verifyReq = item.verifyReq;
        pending.sentAtMs = TimerMgr::NowMs();
        g_recordConnByVerifySeq[item.verifyReq.requestSeq] = pending;
        LOG_INFO("登录外联: 已转发票据校验 seq=%u recordConn=%u",
                 item.verifyReq.requestSeq, item.recordConn);
        return true;
    }
    case OutboxKind::GatewayRegister:
    {
        if (isVerifyTransportBusy())
            return false;
        if (!login->SendMsg(static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_REGISTER_REQ),
                            reinterpret_cast<const char*>(&item.gatewayBody),
                            sizeof(item.gatewayBody)))
        {
            replyGatewayWrapFailed(super, item.gatewayWrapConn, item.gatewayBody.gatewayServerId,
                                   "登录网关注册转发失败");
            return true;
        }
        LOG_INFO("登录外联: 网关包装消息已转发 id=%u conn=%u",
                 item.gatewayBody.gatewayServerId, item.gatewayWrapConn);
        return true;
    }
    case OutboxKind::GatewayHeartbeat:
    {
        if (isVerifyTransportBusy())
            return false;
        if (item.rawBody.empty())
            return true;
        if (!login->SendMsg(static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_HEARTBEAT),
                            item.rawBody.data(),
                            static_cast<uint16_t>(item.rawBody.size())))
            return false;
        return true;
    }
    case OutboxKind::UpdateLastUser:
    {
        if (isVerifyTransportBusy())
            return false;
        if (item.rawBody.empty())
            return true;
        if (!login->SendMsg(static_cast<uint16_t>(InternalMsgID::LOGIN_UPDATE_LAST_USER_REQ),
                            item.rawBody.data(),
                            static_cast<uint16_t>(item.rawBody.size())))
            return false;
        return true;
    }
    case OutboxKind::ZoneStatusReport:
    {
        if (isVerifyTransportBusy())
            return false;
        if (!login->SendMsg(static_cast<uint16_t>(InternalMsgID::LOGIN_ZONE_STATUS_REPORT),
                            reinterpret_cast<const char*>(&item.zoneReport),
                            sizeof(item.zoneReport)))
            return false;
        LOG_DEBUG("区状态上报: zone=%u online=%u gateways=%u alive=%u",
                  item.zoneReport.zoneId, item.zoneReport.onlineCount,
                  item.zoneReport.gatewayCount, item.zoneReport.alive);
        return true;
    }
    case OutboxKind::ExternGameZoneFwd:
    {
        if (isVerifyTransportBusy())
            return false;
        if (item.rawBody.empty())
            return true;
        if (!login->SendMsg(static_cast<uint16_t>(InternalMsgID::EXT_GAMEZONE_FWD_REQ),
                            item.rawBody.data(),
                            static_cast<uint16_t>(item.rawBody.size())))
            return false;
        return true;
    }
    default:
        return true;
    }
}

} // namespace

namespace LoginExternOutbox
{

bool enqueueVerifyToken(SuperServer& super, ConnID recordConn,
                        const Msg_Login_VerifyTokenReq& req)
{
    if (g_outbox.size() >= MAX_OUTBOX_SIZE)
    {
        sendVerifyTokenFailToRecord(super, recordConn, req, "登录外联出站队列已满");
        return false;
    }
    OutboxItem item{};
    item.kind = OutboxKind::VerifyToken;
    item.enqueuedAtMs = TimerMgr::NowMs();
    item.recordConn = recordConn;
    item.verifyReq = req;
    g_outbox.push_front(item);
    LOG_DEBUG("登录外联: 票据校验入队 seq=%u recordConn=%u", req.requestSeq, recordConn);
    return true;
}

void enqueueGatewayRegister(SuperServer& super, ConnID gatewayWrapConn,
                            const Msg_Login_GatewayRegister& body)
{
    if (g_outbox.size() >= MAX_OUTBOX_SIZE)
    {
        replyGatewayWrapFailed(super, gatewayWrapConn, body.gatewayServerId,
                               "登录外联出站队列已满");
        return;
    }
    OutboxItem item{};
    item.kind = OutboxKind::GatewayRegister;
    item.enqueuedAtMs = TimerMgr::NowMs();
    item.gatewayWrapConn = gatewayWrapConn;
    item.gatewayBody = body;
    g_outbox.push_back(item);
}

void enqueueGatewayHeartbeat(const char* data, uint16_t len)
{
    if (!data || len == 0)
        return;
    if (g_outbox.size() >= MAX_OUTBOX_SIZE)
        return;
    OutboxItem item{};
    item.kind = OutboxKind::GatewayHeartbeat;
    item.enqueuedAtMs = TimerMgr::NowMs();
    item.rawBody.assign(data, data + len);
    g_outbox.push_back(item);
}

void enqueueUpdateLastUser(const char* data, uint16_t len)
{
    if (!data || len == 0)
        return;
    if (g_outbox.size() >= MAX_OUTBOX_SIZE)
        return;
    OutboxItem item{};
    item.kind = OutboxKind::UpdateLastUser;
    item.enqueuedAtMs = TimerMgr::NowMs();
    item.rawBody.assign(data, data + len);
    g_outbox.push_back(item);
}

void enqueueZoneStatusReport(const Msg_Login_ZoneStatusReport& report)
{
    if (g_outbox.size() >= MAX_OUTBOX_SIZE)
        return;
    OutboxItem item{};
    item.kind = OutboxKind::ZoneStatusReport;
    item.enqueuedAtMs = TimerMgr::NowMs();
    item.zoneReport = report;
    g_outbox.push_back(item);
}

void enqueueExternGameZoneFwd(const char* data, uint16_t len)
{
    if (!data || len == 0)
        return;
    if (g_outbox.size() >= MAX_OUTBOX_SIZE)
    {
        LOG_WARN("登录外联: 出站队列已满，丢弃 EXT_GAMEZONE_FWD len=%u", len);
        return;
    }
    OutboxItem item{};
    item.kind = OutboxKind::ExternGameZoneFwd;
    item.enqueuedAtMs = TimerMgr::NowMs();
    item.rawBody.assign(data, data + len);
    g_outbox.push_back(item);
}

bool hasPendingVerify()
{
    if (!g_recordConnByVerifySeq.empty())
        return true;
    for (const OutboxItem& item : g_outbox)
    {
        if (isVerifyItem(item))
            return true;
    }
    return false;
}
void completeVerifyRsp(SuperServer& super, const Msg_Login_VerifyTokenRsp& rsp)
{
    auto it = g_recordConnByVerifySeq.find(rsp.requestSeq);
    if (it == g_recordConnByVerifySeq.end())
        return;
    super.tcpServer().SendMsg(it->second.recordConn,
                              static_cast<uint16_t>(InternalMsgID::REC_VERIFY_TOKEN_RSP),
                              reinterpret_cast<const char*>(&rsp), sizeof(rsp));
    g_recordConnByVerifySeq.erase(it);
}

void onExternTick(SuperServer& super)
{
    const uint64_t nowMs = TimerMgr::NowMs();
    TcpClient* login = super.externHub().client(SubServerType::LOGIN);

    const bool loginConnected = login && login->IsConnected();
    if (loginConnected && login->canSend())
    {
        if (g_loginConnReadyMs == 0)
            g_loginConnReadyMs = nowMs;
    }
    else if (!loginConnected)
    {
        g_loginConnReadyMs = 0;
        requeueInFlightVerifyOnDisconnect(super);
        resetQueuedVerifySendState();
    }

    for (auto it = g_recordConnByVerifySeq.begin(); it != g_recordConnByVerifySeq.end(); )
    {
        if (nowMs - it->second.sentAtMs >= DEFERRED_VERIFY_TIMEOUT_MS)
        {
            sendVerifyTokenFailToRecord(super, it->second.recordConn, it->second.verifyReq,
                                        "校验回包超时");
            it = g_recordConnByVerifySeq.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = g_outbox.begin(); it != g_outbox.end(); )
    {
        if (isVerifyItem(*it) && nowMs - it->enqueuedAtMs >= DEFERRED_VERIFY_TIMEOUT_MS)
        {
            sendVerifyTokenFailToRecord(super, it->recordConn, it->verifyReq, "出站队列超时");
            it = g_outbox.erase(it);
            continue;
        }
        ++it;
    }

    size_t flushed = 0;
    for (auto it = g_outbox.begin(); it != g_outbox.end() && flushed < MAX_FLUSH_PER_TICK; )
    {
        if (isVerifyItem(*it) && !canSendVerifyNow())
        {
            ++it;
            continue;
        }
        if (!isVerifyItem(*it) && isVerifyTransportBusy())
        {
            ++it;
            continue;
        }
        if (trySendItem(super, *it))
        {
            it = g_outbox.erase(it);
            ++flushed;
        }
        else
        {
            ++it;
        }
    }
}

} // namespace LoginExternOutbox
