/**
 * @file    SuperLoginMsg.cpp
 * @brief  SuperServer Login 网关代理实现
 */

#include "SuperLoginMsg.h"
#include "LoginExternOutbox.h"
#include "SuperServer.h"
#include "../sdk/util/ExternalServerHub.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../sdk/log/Logger.h"

#include <unordered_map>

namespace
{
std::unordered_map<uint32_t, ConnID> g_gatewayConnByServerId;
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

    TcpClient* login = super.externHub().client(SubServerType::LOGIN);
    if (!login || !login->canSend())
    {
        Msg_SS_LoginGatewayWrapRsp rsp{};
        rsp.gatewayConnID = fromConn;
        rsp.body.code = -1;
        rsp.body.gatewayServerId = wrap->body.gatewayServerId;
        super.tcpServer().SendMsg(fromConn,
            static_cast<uint16_t>(InternalMsgID::SS_LOGIN_GATEWAY_WRAP_RSP),
            reinterpret_cast<char*>(&rsp), sizeof(rsp));
        LOG_WARN("登录外联: 登录服未连接 id=%u", wrap->body.gatewayServerId);
        return;
    }

    LoginExternOutbox::enqueueGatewayRegister(super, fromConn, wrap->body);
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
    (void)super;
    LoginExternOutbox::enqueueGatewayHeartbeat(data, len);
}

void superLoginOnVerifyTokenReq(SuperServer& super, ConnID fromConn,
                                const char* data, uint16_t len)
{
    (void)super;
    if (len < sizeof(Msg_Login_VerifyTokenReq))
        return;
    const auto* req = reinterpret_cast<const Msg_Login_VerifyTokenReq*>(data);
    LoginExternOutbox::enqueueVerifyToken(super, fromConn, *req);
}

void superLoginOnVerifyTokenRsp(SuperServer& super, ConnID /*fromLoginConn*/,
                                const char* data, uint16_t len)
{
    if (len < sizeof(Msg_Login_VerifyTokenRsp))
        return;
    const auto* rsp = reinterpret_cast<const Msg_Login_VerifyTokenRsp*>(data);
    LoginExternOutbox::completeVerifyRsp(super, *rsp);
}

void superLoginOnUpdateLastUserReq(SuperServer& super, ConnID /*fromConn*/,
                                   const char* data, uint16_t len)
{
    (void)super;
    LoginExternOutbox::enqueueUpdateLastUser(data, len);
}

bool superLoginHasPendingVerify()
{
    return LoginExternOutbox::hasPendingVerify();
}

void superLoginOnExternTick(SuperServer& super)
{
    LoginExternOutbox::onExternTick(super);
}
