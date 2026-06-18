/**
 * @file    LoginGameZoneAuthMsg.cpp
 * @brief   LoginServer 处理区内服票据校验与账号最近角色回填
 */

#include "LoginGameZoneAuthMsg.h"
#include "LoginServer.h"
#include "../sdk/log/Logger.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../sdk/util/WireStringUtil.h"
#include "../protocal/InternalMsg.h"

#include <cstring>

namespace
{

void onVerifyTokenReq(LoginServer& server, ConnID fromConn, const Msg_Login_VerifyTokenReq& req)
{
    Msg_Login_VerifyTokenRsp rsp{};
    rsp.requestSeq = req.requestSeq;
    rsp.code = 1;
    rsp.accid = 0;

    MYSQL* db = server.db();
    if (!db)
    {
        server.registerServer().SendMsg(fromConn,
            static_cast<uint16_t>(InternalMsgID::LOGIN_VERIFY_TOKEN_RSP),
            reinterpret_cast<char*>(&rsp), sizeof(rsp));
        return;
    }

    char token[sizeof(req.loginToken)];
    copyToWire(token, sizeof(token), req.loginToken);
    if (token[0] == '\0')
    {
        server.registerServer().SendMsg(fromConn,
            static_cast<uint16_t>(InternalMsgID::LOGIN_VERIFY_TOKEN_RSP),
            reinterpret_cast<char*>(&rsp), sizeof(rsp));
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

    server.registerServer().SendMsg(fromConn,
        static_cast<uint16_t>(InternalMsgID::LOGIN_VERIFY_TOKEN_RSP),
        reinterpret_cast<char*>(&rsp), sizeof(rsp));
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
