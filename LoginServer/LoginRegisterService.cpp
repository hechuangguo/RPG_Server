/**
 * @file    LoginRegisterService.cpp
 * @brief   LoginServer 客户端注册：C2SRegisterReq Protobuf 解析与 GameUser 落库
 */

#include "LoginRegisterService.h"
#include "LoginServer.h"
#include "ClientCommon.pb.h"
#include "LoginMsg.pb.h"
#include "../sdk/log/Logger.h"
#include "../sdk/util/PasswordUtil.h"
#include "../sdk/util/PasswordDigestUtil.h"
#include "../sdk/net/ClientWireSend.h"
#include "../sdk/net/ClientProtoWire.h"

#include <mysqld_error.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
constexpr int32_t REGISTER_OK = 0;
constexpr int32_t REGISTER_ACCOUNT_EXISTS = 1;
constexpr int32_t REGISTER_BAD_PARAM = 2;
constexpr int32_t REGISTER_ZONE_UNAVAILABLE = 3;
constexpr int32_t REGISTER_SERVER_ERROR = -1;

constexpr uint8_t kLoginModule = static_cast<uint8_t>(rpg::client::LOGIN);

bool isPrintableAscii(const std::string& str)
{
    if (str.empty())
        return false;
    for (unsigned char ch : str)
    {
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
    rpg::login::S2CRegisterRsp rsp;
    rsp.set_code(code);
    rsp.set_accid(accid);
    rsp.set_msg(msg ? msg : "");
    sendClientProtoModule(m_owner.clientServer(), connID, kLoginModule,
                    static_cast<uint8_t>(rpg::login::S2C_REGISTER_RSP), rsp);
}

void LoginRegisterService::onClientRegister(ConnID connID, const char* data, uint16_t len)
{
    rpg::login::C2SRegisterReq req;
    if (!parseProto(data, len, req))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "注册包体非法");
        return;
    }

    const std::string& account = req.account();

    uint8_t passwordDigest[PASSWORD_DIGEST_LEN]{};
    uint8_t confirmDigest[PASSWORD_DIGEST_LEN]{};
    if (!copyWireDigest(req.password_digest(), passwordDigest) ||
        !copyWireDigest(req.confirm_password_digest(), confirmDigest))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "密码摘要非法");
        return;
    }

    if (looksLikePlaintextPassword(passwordDigest) ||
        looksLikePlaintextPassword(confirmDigest))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "请升级客户端（需发送密码摘要）");
        return;
    }
    if (!digestsEqual(passwordDigest, confirmDigest))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "两次密码不一致");
        return;
    }
    if (isZeroDigest(passwordDigest))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "密码摘要非法");
        return;
    }
    if (!isPrintableAscii(account))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "账号格式非法");
        return;
    }
    if (!m_owner.peekLoginChallengeNonce(connID, req.login_nonce()))
    {
        sendRegisterRsp(connID, REGISTER_BAD_PARAM, "登录挑战无效");
        LOG_WARN("注册挑战校验失败: conn=%u loginNonceLen=%zu",
                 connID, req.login_nonce().size());
        return;
    }
    if (!m_owner.zoneInfoStore().isZoneEnabled(static_cast<uint8_t>(req.game_type()), req.zone_id()))
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

    char escAccount[256];
    mysql_real_escape_string(db, escAccount, account.c_str(), account.size());

    const std::string querySql =
        "SELECT accid FROM GameUser WHERE account='" + std::string(escAccount) + "' LIMIT 1";
    if (mysql_query(db, querySql.c_str()) != 0)
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
    if (!hashPasswordDigestBcrypt(passwordDigest, passwordHash))
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
                  escAccount, escHash, req.zone_id());
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
             connID, static_cast<unsigned long long>(accid), account.c_str(),
             req.zone_id(), req.game_type());
}
