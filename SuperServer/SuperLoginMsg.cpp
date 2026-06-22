/**
 * @file    SuperLoginMsg.cpp
 * @brief  SuperServer Login 网关代理实现
 */

#include "SuperLoginMsg.h"
#include "SuperServer.h"
#include "../sdk/util/ExternalServerHub.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../sdk/log/Logger.h"

#include <unordered_map>

namespace
{
std::unordered_map<uint32_t, ConnID> g_gatewayConnByServerId;
std::unordered_map<uint32_t, ConnID> g_recordConnByVerifySeq;

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
} // namespace

void SuperLoginMsgRegister(SuperServer& super)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalFree(d, super,
                         static_cast<uint16_t>(InternalMsgID::SS_LOGIN_GATEWAY_WRAP_REQ),
                         superLoginOnGatewayWrapReq);
    registerInternalFree(d, super,
                         static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_HEARTBEAT),
                         superLoginOnGatewayHeartbeat);
    registerInternalFree(d, super,
                         static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_REGISTER_RSP),
                         superLoginOnGatewayRegisterRsp);
    registerInternalFree(d, super,
                         static_cast<uint16_t>(InternalMsgID::LOGIN_VERIFY_TOKEN_REQ),
                         superLoginOnVerifyTokenReq);
    registerInternalFree(d, super,
                         static_cast<uint16_t>(InternalMsgID::LOGIN_VERIFY_TOKEN_RSP),
                         superLoginOnVerifyTokenRsp);
    registerInternalFree(d, super,
                         static_cast<uint16_t>(InternalMsgID::LOGIN_UPDATE_LAST_USER_REQ),
                         superLoginOnUpdateLastUserReq);
}

void superLoginOnGatewayWrapReq(SuperServer& super, ConnID fromConn,
                                const char* data, uint16_t len)
{
    if (len < sizeof(Msg_SS_LoginGatewayWrap))
        return;

    const auto* wrap = reinterpret_cast<const Msg_SS_LoginGatewayWrap*>(data);
    g_gatewayConnByServerId[wrap->body.gatewayServerId] = fromConn;

    auto replyGatewayWrapFailed = [&](const char* reason)
    {
        LOG_WARN("登录外联: %s id=%u", reason, wrap->body.gatewayServerId);
        Msg_SS_LoginGatewayWrapRsp rsp{};
        rsp.gatewayConnID = fromConn;
        rsp.body.code = -1;
        rsp.body.gatewayServerId = wrap->body.gatewayServerId;
        super.tcpServer().SendMsg(fromConn,
            static_cast<uint16_t>(InternalMsgID::SS_LOGIN_GATEWAY_WRAP_RSP),
            reinterpret_cast<char*>(&rsp), sizeof(rsp));
    };

    TcpClient* login = super.externHub().client(SubServerType::LOGIN);
    if (!login || !login->IsConnected())
    {
        replyGatewayWrapFailed("登录服未连接");
        return;
    }
    if (!login->canSend())
    {
        replyGatewayWrapFailed("登录外联 TLS 未就绪");
        return;
    }

    if (!login->SendMsg(static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_REGISTER_REQ),
                        reinterpret_cast<const char*>(&wrap->body), sizeof(wrap->body)))
    {
        replyGatewayWrapFailed("登录网关注册转发失败");
        return;
    }
    LOG_INFO("登录外联: 网关包装消息已转发 id=%u conn=%u",
             wrap->body.gatewayServerId, wrap->gatewayConnID);
}

void superLoginOnGatewayRegisterRsp(SuperServer& super, ConnID /*fromLoginConn*/,
                                    const char* data, uint16_t len)
{
    if (len < sizeof(Msg_Login_GatewayRegisterRsp))
        return;

    const auto* body = reinterpret_cast<const Msg_Login_GatewayRegisterRsp*>(data);
    auto it = g_gatewayConnByServerId.find(body->gatewayServerId);
    if (it == g_gatewayConnByServerId.end())
    {
        LOG_WARN("登录外联: 未找到网关路由 id=%u", body->gatewayServerId);
        return;
    }

    Msg_SS_LoginGatewayWrapRsp rsp{};
    rsp.gatewayConnID = it->second;
    rsp.body = *body;
    super.tcpServer().SendMsg(it->second,
        static_cast<uint16_t>(InternalMsgID::SS_LOGIN_GATEWAY_WRAP_RSP),
        reinterpret_cast<char*>(&rsp), sizeof(rsp));
}

void superLoginOnGatewayHeartbeat(SuperServer& super, ConnID /*fromConn*/,
                                  const char* data, uint16_t len)
{
    TcpClient* login = super.externHub().client(SubServerType::LOGIN);
    if (!login || !login->canSend())
        return;

    login->SendMsg(static_cast<uint16_t>(InternalMsgID::LOGIN_GATEWAY_HEARTBEAT),
                   data, len);
}

void superLoginOnVerifyTokenReq(SuperServer& super, ConnID fromConn,
                                const char* data, uint16_t len)
{
    if (len < sizeof(Msg_Login_VerifyTokenReq))
        return;
    const auto* req = reinterpret_cast<const Msg_Login_VerifyTokenReq*>(data);
    TcpClient* login = super.externHub().client(SubServerType::LOGIN);
    ExternalServerConnector* loginConn = super.externHub().connector(SubServerType::LOGIN);
    if (!login || !login->canSend())
    {
        sendVerifyTokenFailToRecord(super, fromConn, *req, "登录服未就绪");
        return;
    }
    if (!login->SendMsg(static_cast<uint16_t>(InternalMsgID::LOGIN_VERIFY_TOKEN_REQ), data, len))
    {
        sendVerifyTokenFailToRecord(super, fromConn, *req, "SendMsg失败");
        return;
    }
    if (loginConn)
        loginConn->poll();
    if (!login->IsConnected())
    {
        sendVerifyTokenFailToRecord(super, fromConn, *req, "发送后连接断开");
        return;
    }
    g_recordConnByVerifySeq[req->requestSeq] = fromConn;
    LOG_INFO("登录外联: 已转发票据校验 seq=%u recordConn=%u", req->requestSeq, fromConn);
}

void superLoginOnVerifyTokenRsp(SuperServer& super, ConnID /*fromLoginConn*/,
                                const char* data, uint16_t len)
{
    if (len < sizeof(Msg_Login_VerifyTokenRsp))
        return;
    const auto* rsp = reinterpret_cast<const Msg_Login_VerifyTokenRsp*>(data);
    auto it = g_recordConnByVerifySeq.find(rsp->requestSeq);
    if (it == g_recordConnByVerifySeq.end())
        return;
    super.tcpServer().SendMsg(it->second,
        static_cast<uint16_t>(InternalMsgID::REC_VERIFY_TOKEN_RSP), data, len);
    g_recordConnByVerifySeq.erase(it);
}

void superLoginOnUpdateLastUserReq(SuperServer& super, ConnID /*fromConn*/,
                                   const char* data, uint16_t len)
{
    TcpClient* login = super.externHub().client(SubServerType::LOGIN);
    if (!login || !login->IsConnected())
        return;
    login->SendMsg(static_cast<uint16_t>(InternalMsgID::LOGIN_UPDATE_LAST_USER_REQ), data, len);
}

bool superLoginHasPendingVerify()
{
    return !g_recordConnByVerifySeq.empty();
}
