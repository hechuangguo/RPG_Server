/**
 * @file    LoginAuthService.cpp
 * @brief  LoginAuthService 实现：账号登录、区列表与网关地址下发（Protobuf wire）
 */

#include "LoginAuthService.h"
#include "LoginServer.h"
#include "LoginTokenUtil.h"
#include "ClientCommon.pb.h"
#include "LoginMsg.pb.h"
#include "ZoneMsg.pb.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../sdk/util/LoginSpawnConfig.h"
#include "../sdk/util/LoginFlowLog.h"
#include "../sdk/util/PasswordUtil.h"
#include "../sdk/util/PasswordDigestUtil.h"
#include "../sdk/net/ClientWireSend.h"
#include "../sdk/net/ClientProtoWire.h"

#include <mysqld_error.h>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr uint8_t kLoginModule = static_cast<uint8_t>(rpg::client::LOGIN);

} // namespace

LoginAuthService::LoginAuthService(LoginServer& owner)
    : m_owner(owner)
{
}

void LoginAuthService::onClientZoneList(ConnID connID, const char* data, uint16_t len)
{
    uint8_t gameTypeFilter = static_cast<uint8_t>(ZONE_LIST_ALL_GAME_TYPES);
    rpg::zone::C2SZoneListReq protoReq;
    if (parseProto(data, len, protoReq))
        gameTypeFilter = static_cast<uint8_t>(protoReq.game_type());
    else if (len >= 1)
    {
        gameTypeFilter = static_cast<uint8_t>(data[0]);
        LOG_DEBUG("区列表请求非 Protobuf，按首字节 gameType=0x%02X: conn=%u",
                  gameTypeFilter, connID);
    }

    std::vector<ZoneInfoRow> zones;
    m_owner.zoneInfoStore().listAll(zones, gameTypeFilter);

    rpg::zone::S2CZoneListRsp rsp;
    if (zones.size() > MAX_ZONE_LIST_ENTRIES)
    {
        rsp.set_code(-1);
        LOG_ERR("区列表条目过多: %zu（上限 %u）", zones.size(), MAX_ZONE_LIST_ENTRIES);
        sendClientProtoModule(m_owner.clientServer(), connID, kLoginModule,
                       static_cast<uint8_t>(rpg::zone::S2C_ZONE_LIST_RSP), rsp);
        return;
    }

    rsp.set_code(0);
    auto& registry = m_owner.gatewayRegistry();
    for (const ZoneInfoRow& row : zones)
    {
        auto* entry = rsp.add_entries();
        entry->set_zone_id(row.zoneId);
        entry->set_game_type(row.gameType);
        entry->set_enabled(row.enabled ? 1 : 0);
        entry->set_name(row.name);
        entry->set_ip(row.ip);
        entry->set_super_port(row.superPort);

        uint32_t onlineCount = 0;
        uint8_t gatewayCount = 0;
        uint8_t loadLevel = 0;
        ZoneInfoStore::fillGatewayLoadFields(row, m_owner.zoneInfoStore(),
                                             registry.countForZone(row.zoneId, row.gameType),
                                             onlineCount, gatewayCount, loadLevel);
        entry->set_online_count(onlineCount);
        entry->set_gateway_count(gatewayCount);
        entry->set_load_level(static_cast<rpg::zone::ZoneLoadLevel>(loadLevel));

        const uint64_t nowMs = TimerMgr::NowMs();
        ZoneRuntimeRow runtime;
        const ZoneRuntimeRow* runtimePtr = nullptr;
        if (m_owner.zoneInfoStore().getRuntime(row.gameType, row.zoneId, runtime))
            runtimePtr = &runtime;
        const uint64_t runtimeAgeMs = runtimePtr && nowMs >= runtimePtr->lastReportMs
            ? (nowMs - runtimePtr->lastReportMs) : 0;
        LOG_DEBUG("区列表判定: zone=%u gameType=%u enabled=%u online=%u gateway=%u runtimeAlive=%u runtimeAgeMs=%llu loadLevel=%u",
                  row.zoneId, row.gameType, row.enabled ? 1 : 0, onlineCount, gatewayCount,
                  runtimePtr && runtimePtr->alive ? 1 : 0,
                  static_cast<unsigned long long>(runtimeAgeMs), loadLevel);
    }

    sendClientProtoModule(m_owner.clientServer(), connID, kLoginModule,
                   static_cast<uint8_t>(rpg::zone::S2C_ZONE_LIST_RSP), rsp);
    LOG_INFO("已下发区列表: conn=%u count=%d filter=0x%02X",
             connID, rsp.entries_size(), gameTypeFilter);
}

void LoginAuthService::onClientLogin(ConnID connID, const char* data, uint16_t len)
{
    rpg::login::C2SLoginReq req;
    if (!parseProto(data, len, req))
    {
        rpg::login::S2CLoginRsp loginRsp;
        loginRsp.set_code(1);
        loginRsp.set_msg("请求格式错误");
        sendClientProtoModule(m_owner.clientServer(), connID, kLoginModule,
                       static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);
        logLoginFlow(LoginFlowPhase::ACCOUNT_LOGIN, 0, 0, connID, loginRsp.code(),
                     loginRsp.msg().c_str());
        sendGatewayInfo(connID, -1, "登录失败");
        return;
    }

    rpg::login::S2CLoginRsp loginRsp;
    loginRsp.set_user_id(0);
    loginRsp.set_accid(0);

    if (!m_owner.peekLoginChallengeNonce(connID, req.login_nonce()))
    {
        loginRsp.set_code(1);
        loginRsp.set_msg("登录挑战无效");
        sendClientProtoModule(m_owner.clientServer(), connID, kLoginModule,
                       static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);
        logLoginFlow(LoginFlowPhase::ACCOUNT_LOGIN, 0, 0, connID, loginRsp.code(),
                     loginRsp.msg().c_str());
        sendGatewayInfo(connID, -1, "登录失败");
        LOG_WARN("登录挑战校验失败: conn=%u loginNonceLen=%zu",
                 connID, req.login_nonce().size());
        return;
    }

    if (m_owner.dbRequired() && !m_owner.db())
    {
        loginRsp.set_code(-1);
        loginRsp.set_msg("登录服不可用");
        sendClientProtoModule(m_owner.clientServer(), connID, kLoginModule,
                       static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);
        logLoginFlow(LoginFlowPhase::ACCOUNT_LOGIN, 0, 0, connID, loginRsp.code(),
                     loginRsp.msg().c_str());
        sendGatewayInfo(connID, -1, "无可用网关");
        return;
    }

    const std::string& account = req.account();

    uint8_t passwordDigest[PASSWORD_DIGEST_LEN]{};
    if (!copyWireDigest(req.password_digest(), passwordDigest))
    {
        loginRsp.set_code(1);
        loginRsp.set_msg("密码摘要非法");
        sendClientProtoModule(m_owner.clientServer(), connID, kLoginModule,
                       static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);
        logLoginFlow(LoginFlowPhase::ACCOUNT_LOGIN, 0, 0, connID, loginRsp.code(),
                     loginRsp.msg().c_str());
        sendGatewayInfo(connID, -1, "登录失败");
        return;
    }

    if (looksLikePlaintextPassword(passwordDigest))
    {
        loginRsp.set_code(1);
        loginRsp.set_msg("请升级客户端（需发送密码摘要）");
        sendClientProtoModule(m_owner.clientServer(), connID, kLoginModule,
                       static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);
        logLoginFlow(LoginFlowPhase::ACCOUNT_LOGIN, 0, 0, connID, loginRsp.code(),
                     loginRsp.msg().c_str());
        sendGatewayInfo(connID, -1, "登录失败");
        return;
    }

    MYSQL* db = m_owner.db();
    if (!db)
    {
        loginRsp.set_code(-1);
        loginRsp.set_msg("数据库不可用");
    }
    else
    {
        char escAccount[256];
        mysql_real_escape_string(db, escAccount, account.c_str(), account.size());

        char sql[384];
        snprintf(sql, sizeof(sql),
                 "SELECT accid, password_hash, user_id, gamezone FROM GameUser "
                 "WHERE account='%s' LIMIT 1", escAccount);
        if (mysql_query(db, sql) != 0)
        {
            LOG_ERR("查询 GameUser 失败: %s", mysql_error(db));
            loginRsp.set_code(-1);
            loginRsp.set_msg("数据库错误");
        }
        else
        {
            MYSQL_RES* res = mysql_store_result(db);
            MYSQL_ROW row = res ? mysql_fetch_row(res) : nullptr;
            if (!row)
            {
                loginRsp.set_code(1);
                loginRsp.set_msg("账号不存在");
            }
            else
            {
                const char* hash = row[1] ? row[1] : "";
                const uint32_t gameZone = row[3] ? static_cast<uint32_t>(strtoul(row[3], nullptr, 10)) : 0;
                if (!verifyPasswordDigestBcrypt(passwordDigest, hash))
                {
                    loginRsp.set_code(1);
                    loginRsp.set_msg("账号或密码错误");
                    LOG_WARN("密码校验失败: conn=%u account=%s（password_digest 须为 SHA-256(密码) 32 字节，"
                             "nonce 仅放在 login_nonce；勿使用 SHA-256(nonce||密码)）",
                             connID, account.c_str());
                }
                else if (gameZone != 0 && gameZone != req.zone_id())
                {
                    loginRsp.set_code(1);
                    loginRsp.set_msg("区服不匹配");
                }
                else
                {
                    loginRsp.set_code(0);
                    loginRsp.set_accid(row[0] ? static_cast<uint64_t>(strtoull(row[0], nullptr, 10)) : 0);
                    loginRsp.set_user_id(row[2] ? static_cast<uint64_t>(strtoull(row[2], nullptr, 10)) : 0);
                    loginRsp.set_msg("登录成功");

                    char token[65] = {};
                    if (!generateLoginToken(token, sizeof(token)))
                    {
                        loginRsp.set_code(-1);
                        loginRsp.set_msg("登录票据生成失败");
                    }
                    else
                    {
                        char escToken[sizeof(token) * 2 + 1];
                        mysql_real_escape_string(db, escToken, token, strlen(token));
                        snprintf(sql, sizeof(sql),
                                 "REPLACE INTO LoginSession (token, accid, zone_id, game_type, expire_time) "
                                 "VALUES ('%s', %llu, %u, %u, DATE_ADD(NOW(), INTERVAL %u SECOND))",
                                 escToken,
                                 static_cast<unsigned long long>(loginRsp.accid()),
                                 req.zone_id(), req.game_type(), LOGIN_TOKEN_TTL_SEC);
                        if (mysql_query(db, sql) != 0)
                        {
                            const unsigned int err = mysql_errno(db);
                            LOG_ERR("写入 LoginSession 失败: %s", mysql_error(db));
                            loginRsp.set_code(-1);
                            if (err == ER_NO_SUCH_TABLE)
                                loginRsp.set_msg("会话表缺失，请执行 tables/migrate_login_session.sql");
                            else
                                loginRsp.set_msg("会话写入失败");
                        }
                        else
                        {
                            loginRsp.set_login_token(token);
                            loginRsp.set_token_expire_ms(
                                TimerMgr::NowMs() + static_cast<uint64_t>(LOGIN_TOKEN_TTL_SEC) * 1000ULL);
                        }
                    }
                }
            }
            if (res)
                mysql_free_result(res);
        }
    }

    sendClientProtoModule(m_owner.clientServer(), connID, kLoginModule,
                   static_cast<uint8_t>(rpg::login::S2C_LOGIN_RSP), loginRsp);
    if (loginRsp.code() == 0)
    {
        m_owner.verifyAndConsumeLoginNonce(connID, req.login_nonce());
        LOG_INFO("账号登录成功: connID=%u zone=%u gameType=%u userID=%llu",
                 connID, req.zone_id(), req.game_type(),
                 static_cast<unsigned long long>(loginRsp.user_id()));
        logLoginFlow(LoginFlowPhase::ACCOUNT_LOGIN, loginRsp.accid(), loginRsp.user_id(), connID, 0,
                     nullptr);
    }
    else
    {
        logLoginFlow(LoginFlowPhase::ACCOUNT_LOGIN, loginRsp.accid(), loginRsp.user_id(), connID,
                     loginRsp.code(), loginRsp.msg().c_str());
    }

    if (loginRsp.code() == 0)
        sendGatewayInfo(connID, 0, "成功", req.zone_id(), static_cast<uint8_t>(req.game_type()));
    else
        sendGatewayInfo(connID, -1, "登录失败");
}

void LoginAuthService::sendGatewayInfo(ConnID connID, int32_t code, const char* msg,
                                       uint32_t zoneId, uint8_t gameType)
{
    rpg::login::S2CGatewayInfo info;
    info.set_code(code);
    info.set_msg(msg ? msg : "");
    if (code == 0)
    {
        if (zoneId == 0)
        {
            info.set_code(-1);
            info.set_msg("区服无效");
        }
        else
        {
            auto& zoneStore = m_owner.zoneInfoStore();
            auto& registry = m_owner.gatewayRegistry();

            ZoneInfoRow zone;
            if (!zoneStore.findZone(gameType, zoneId, zone))
            {
                info.set_code(-1);
                info.set_msg("区服不存在");
            }
            else if (!zone.enabled)
            {
                info.set_code(-1);
                info.set_msg("区服维护中");
            }
            else
            {
                uint32_t onlineCount = 0;
                uint8_t gatewayCount = 0;
                uint8_t loadLevel = 0;
                ZoneInfoStore::fillGatewayLoadFields(zone, zoneStore,
                                                     registry.countForZone(zoneId, gameType),
                                                     onlineCount, gatewayCount, loadLevel);
                if (loadLevel == static_cast<uint8_t>(rpg::zone::ZONE_LOAD_MAINTENANCE))
                {
                    info.set_code(-1);
                    info.set_msg("区服不可用");
                }
                else
                {
                    LoginGatewayEntry gw;
                    if (registry.pickByZone(zoneId, gameType, gw))
                    {
                        std::string clientGatewayIp = gw.ip;
                        if (clientGatewayIp.empty() || clientGatewayIp == "127.0.0.1" ||
                            clientGatewayIp == "0.0.0.0")
                        {
                            clientGatewayIp = zone.ip;
                        }
                        info.set_gateway_ip(clientGatewayIp);
                        info.set_gateway_port(gw.port);
                        if (!zone.name.empty())
                            info.set_msg(zone.name);
                    }
                    else
                    {
                        const size_t zoneGwCount = registry.countForZone(zoneId, gameType);
                        LOG_WARN("无可用网关: zone=%u gameType=%u zoneGatewayCount=%zu registryTotal=%zu "
                                 "（检查 Gateway 进程、Super→Login 外联、login.log 网关注册成功）",
                                 zoneId, gameType, zoneGwCount, registry.size());
                        info.set_code(-1);
                        info.set_msg("无可用网关");
                    }
                }
            }
        }
    }
    sendClientProtoModule(m_owner.clientServer(), connID, kLoginModule,
                   static_cast<uint8_t>(rpg::login::S2C_GATEWAY_INFO), info);
    LOG_INFO("已下发网关信息: conn=%u code=%d ip=%s port=%u",
             connID, info.code(), info.gateway_ip().c_str(), info.gateway_port());
    if (info.code() == 0)
    {
        char detail[80];
        snprintf(detail, sizeof(detail), "gateway=%s:%u",
                 info.gateway_ip().c_str(), info.gateway_port());
        logLoginFlow(LoginFlowPhase::ACCOUNT_LOGIN, 0, 0, connID, 0, detail);
    }
    else
    {
        logLoginFlow(LoginFlowPhase::ACCOUNT_LOGIN, 0, 0, connID, info.code(), info.msg().c_str());
    }
}
