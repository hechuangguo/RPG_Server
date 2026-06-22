/**
 * @file    LoginGameZoneAuthMsg.cpp
 * @brief   LoginServer 处理区内服票据校验与账号最近角色回填
 */

#include "LoginGameZoneAuthMsg.h"
#include "LoginServer.h"
#include "../sdk/log/Logger.h"
#include "../sdk/util/GameZoneReply.h"
#include "../sdk/util/LoginFlowLog.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../sdk/util/WireStringUtil.h"
#include "../protocal/InternalMsg.h"

#include <cstring>

namespace
{

/**
 * @brief 回送票据校验结果：优先 EXT_GAMEZONE 对称信封，否则裸 LOGIN_VERIFY_TOKEN_RSP（Super 直连）
 */
void sendVerifyTokenRsp(LoginServer& server, ConnID fromConn,
                        const Msg_Login_VerifyTokenRsp& rsp)
{
    const GameZoneForwardContext* ctx = gameZonePeekForwardContext(fromConn, rsp.requestSeq);
    if (ctx)
    {
        const int32_t envelopeCode = rsp.code == 0 ? 0 : -1;
        const void* innerBody = rsp.code == 0 ? static_cast<const void*>(&rsp) : nullptr;
        const uint16_t innerLen =
            rsp.code == 0 ? static_cast<uint16_t>(sizeof(rsp)) : 0;
        if (!gameZoneSendForwardRsp(server.registerServer(), fromConn, ctx->reqHdr, envelopeCode,
                                    innerBody, innerLen))
        {
            LOG_WARN("登录服票据校验回包发送失败: conn=%u fwdSeq=%u innerSeq=%u",
                     fromConn, ctx->reqHdr.seq, rsp.requestSeq);
        }
        return;
    }
    if (!server.registerServer().SendMsg(
            fromConn, static_cast<uint16_t>(InternalMsgID::LOGIN_VERIFY_TOKEN_RSP),
            reinterpret_cast<const char*>(&rsp), sizeof(rsp)))
    {
        LOG_WARN("登录服票据校验裸回包发送失败: conn=%u seq=%u", fromConn, rsp.requestSeq);
    }
}

void onVerifyTokenReq(LoginServer& server, ConnID fromConn, const Msg_Login_VerifyTokenReq& req)
{
    LOG_DEBUG("登录服收到票据校验: conn=%u seq=%u zone=%u", fromConn, req.requestSeq, req.zoneId);

    Msg_Login_VerifyTokenRsp rsp{};
    rsp.requestSeq = req.requestSeq;
    rsp.code = 1;
    rsp.accid = 0;

    MYSQL* db = server.db();
    if (!db)
    {
        sendVerifyTokenRsp(server, fromConn, rsp);
        return;
    }

    char token[sizeof(req.loginToken)];
    copyToWire(token, sizeof(token), req.loginToken);
    if (token[0] == '\0')
    {
        sendVerifyTokenRsp(server, fromConn, rsp);
        return;
    }

    char escToken[sizeof(token) * 2 + 1];
    mysql_real_escape_string(db, escToken, token, strlen(token));

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT accid FROM LoginSession "
             "WHERE token='%s' AND zone_id=%u AND game_type=%u AND expire_time > NOW() LIMIT 1",
             escToken, req.zoneId, req.gameType);

    if (mysql_query(db, sql) == 0)
    {
        MYSQL_RES* res = mysql_store_result(db);
        MYSQL_ROW row = res ? mysql_fetch_row(res) : nullptr;
        if (row && row[0])
        {
            rsp.code = 0;
            rsp.accid = static_cast<uint64_t>(strtoull(row[0], nullptr, 10));
            snprintf(sql, sizeof(sql), "DELETE FROM LoginSession WHERE token='%s'", escToken);
            mysql_query(db, sql);
        }
        if (res)
            mysql_free_result(res);
    }
    else
    {
        LOG_ERR("登录服票据校验 SQL 失败: %s", mysql_error(db));
    }

    if (rsp.code == 0)
    {
        LOG_INFO("登录服票据校验成功: seq=%u accid=%llu zone=%u",
                 rsp.requestSeq, static_cast<unsigned long long>(rsp.accid), req.zoneId);
        logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, rsp.accid, 0, 0, 0, "Login校验token");
    }
    else
    {
        LOG_WARN("登录服票据校验失败: seq=%u zone=%u gameType=%u",
                 rsp.requestSeq, req.zoneId, req.gameType);
        logLoginFlow(LoginFlowPhase::GATEWAY_AUTH, 0, 0, 0, rsp.code, "Login校验token失败");
    }

    sendVerifyTokenRsp(server, fromConn, rsp);
}

void onUpdateLastUserReq(LoginServer& server, ConnID /*fromConn*/,
                         const Msg_Login_UpdateLastUserReq& req)
{
    MYSQL* db = server.db();
    if (!db)
        return;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "UPDATE GameUser SET user_id=%llu WHERE accid=%llu",
             static_cast<unsigned long long>(req.userID),
             static_cast<unsigned long long>(req.accid));
    if (mysql_query(db, sql) != 0)
    {
        LOG_WARN("登录服回填最近角色失败: accid=%llu userID=%llu err=%s",
                 static_cast<unsigned long long>(req.accid),
                 static_cast<unsigned long long>(req.userID),
                 mysql_error(db));
    }
}

} // namespace

void LoginGameZoneAuthMsgRegister(LoginServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalFree<LoginServer, Msg_Login_VerifyTokenReq>(
        d, server, static_cast<uint16_t>(InternalMsgID::LOGIN_VERIFY_TOKEN_REQ),
        &onVerifyTokenReq);
    registerInternalFree<LoginServer, Msg_Login_UpdateLastUserReq>(
        d, server, static_cast<uint16_t>(InternalMsgID::LOGIN_UPDATE_LAST_USER_REQ),
        &onUpdateLastUserReq);
}
