/**
 * @file    LoginRegisterService.cpp
 * @brief   LoginServer 客户端注册服务实现
 */

#include "LoginRegisterService.h"
#include "LoginServer.h"
#include "../Common/LoginMsg.h"
#include "../Common/ClientMsgBody.h"
#include "../sdk/log/Logger.h"
#include "../sdk/util/PasswordUtil.h"
#include "../sdk/util/PasswordDigestUtil.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/net/ClientWireSend.h"

#include <mysqld_error.h>

#include <cstdio>
#include <cstring>

namespace
{
constexpr int32_t REGISTER_OK = 0;
constexpr int32_t REGISTER_ACCOUNT_EXISTS = 1;
constexpr int32_t REGISTER_BAD_PARAM = 2;
constexpr int32_t REGISTER_ZONE_UNAVAILABLE = 3;
constexpr int32_t REGISTER_SERVER_ERROR = -1;

bool isPrintableAscii(const char* str)
{
    if (!str || str[0] == '\0')
        return false;
    for (size_t i = 0; str[i] != '\0'; ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(str[i]);
        if (ch < 33 || ch > 126)
            return false;
    }
    return true;
}
} // namespace

LoginRegisterService::LoginRegisterService(LoginServer& owner)
    : m_owner(owner)
{
}

void LoginRegisterService::sendRegisterRsp(ConnID connID, int32_t code, const char* msg, uint64_t accid)
{
    Msg_S2C_RegisterRsp rsp{};
    initClientMsg(rsp);
    rsp.code = code;
    rsp.accid = accid;
    copyToWire(rsp.msg, sizeof(rsp.msg), msg);
    sendClientWire(m_owner.clientServer(), connID, rsp);
}

void LoginRegisterService::onClientRegister(ConnID connID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_RegisterReq))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "注册包体非法");
        return;
    }
    const auto* req = reinterpret_cast<const Msg_C2S_RegisterReq*>(data);

    char account[sizeof(req->account)];
    copyToWire(account, sizeof(account), req->account);

    if (looksLikePlaintextPassword(req->passwordDigest) ||
        looksLikePlaintextPassword(req->confirmPasswordDigest))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "请升级客户端（需发送密码摘要）");
        return;
    }
    if (!digestsEqual(req->passwordDigest, req->confirmPasswordDigest))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "两次密码不一致");
        return;
    }
    if (isZeroDigest(req->passwordDigest))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "密码摘要非法");
        return;
    }
    if (!isPrintableAscii(account))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "账号格式非法");
        return;
    }
    if (!m_owner.zoneInfoStore().isZoneEnabled(req->gameType, req->zoneId))
    {
        sendRegisterRsp(connID, REGISTER_ZONE_UNAVAILABLE, "区服不可用");
        return;
    }

    MYSQL* db = m_owner.db();
    if (!db)
    {
        sendRegisterRsp(connID, REGISTER_SERVER_ERROR, "数据库不可用");
        return;
    }

    char escAccount[sizeof(account) * 2 + 1];
    mysql_real_escape_string(db, escAccount, account, std::strlen(account));

    char querySql[256];
    std::snprintf(querySql, sizeof(querySql),
                  "SELECT accid FROM GameUser WHERE account='%s' LIMIT 1", escAccount);
    if (mysql_query(db, querySql) != 0)
    {
        LOG_ERR("注册时查询账号失败: %s", mysql_error(db));
        sendRegisterRsp(connID, REGISTER_SERVER_ERROR, "数据库错误");
        return;
    }

    MYSQL_RES* res = mysql_store_result(db);
    MYSQL_ROW row = res ? mysql_fetch_row(res) : nullptr;
    if (row)
    {
        if (res)
            mysql_free_result(res);
        sendRegisterRsp(connID, REGISTER_ACCOUNT_EXISTS, "账号已存在");
        return;
    }
    if (res)
        mysql_free_result(res);

    std::string passwordHash;
    if (!hashPasswordDigestBcrypt(req->passwordDigest, passwordHash))
    {
        sendRegisterRsp(connID, REGISTER_SERVER_ERROR, "密码哈希失败");
        return;
    }

    char escHash[257];
    mysql_real_escape_string(db, escHash, passwordHash.c_str(),
                             static_cast<unsigned long>(passwordHash.size()));

    char insertSql[768];
    std::snprintf(insertSql, sizeof(insertSql),
                  "INSERT INTO GameUser (account, password_hash, user_id, gamezone) "
                  "VALUES ('%s', '%s', 0, %u)",
                  escAccount, escHash, req->zoneId);
    if (mysql_query(db, insertSql) != 0)
    {
        if (mysql_errno(db) == ER_DUP_ENTRY)
        {
            sendRegisterRsp(connID, REGISTER_ACCOUNT_EXISTS, "账号已存在");
            return;
        }
        LOG_ERR("注册写入 GameUser 失败: %s", mysql_error(db));
        sendRegisterRsp(connID, REGISTER_SERVER_ERROR, "创建账号失败");
        return;
    }

    const uint64_t accid = static_cast<uint64_t>(mysql_insert_id(db));
    sendRegisterRsp(connID, REGISTER_OK, "注册成功", accid);
    LOG_INFO("账号注册成功: connID=%u accid=%llu account=%s zone=%u gameType=%u",
             connID, static_cast<unsigned long long>(accid), account, req->zoneId, req->gameType);
}
