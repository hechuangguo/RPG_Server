/**
 * @file    LoginAuthService.cpp
 * @brief  LoginAuthService 实现
 */

#include "LoginAuthService.h"
#include "LoginServer.h"
#include "LoginTokenUtil.h"
#include "../Common/LoginMsg.h"
#include "../Common/ClientMsgBody.h"
#include "../Common/ZoneMsg.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../sdk/util/LoginSpawnConfig.h"
#include "../sdk/util/LoginFlowLog.h"
#include "../sdk/util/PasswordUtil.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/net/ClientWireSend.h"

#include <cstring>
#include <string>
#include <vector>

LoginAuthService::LoginAuthService(LoginServer& owner)
    : m_owner(owner)
{
}

void LoginAuthService::onClientZoneList(ConnID connID, const char* data, uint16_t len)
{
    uint8_t gameTypeFilter = 0xFF;
    if (len >= sizeof(Msg_C2S_ZoneListReq))
        gameTypeFilter = reinterpret_cast<const Msg_C2S_ZoneListReq*>(data)->gameType;

    std::vector<ZoneInfoRow> zones;
    m_owner.zoneInfoStore().listAll(zones, gameTypeFilter);

    Msg_S2C_ZoneListRspHeader header{};
    initClientMsg(header);
    if (zones.size() > MAX_ZONE_LIST_ENTRIES)
    {
        header.code = -1;
        header.count = 0;
        LOG_ERR("区列表条目过多: %zu（上限 %u）", zones.size(), MAX_ZONE_LIST_ENTRIES);
        m_owner.clientServer().SendMsg(connID, Msg_S2C_ZoneListRspHeader::kModule,
                                       Msg_S2C_ZoneListRspHeader::kSub,
                                       reinterpret_cast<char*>(&header), sizeof(header));
        return;
    }

    header.code = 0;
    header.count = static_cast<uint16_t>(zones.size());

    const size_t bodyLen = sizeof(header) + header.count * sizeof(Msg_S2C_ZoneEntryWire);
    std::vector<char> body(bodyLen);
    std::memcpy(body.data(), &header, sizeof(header));

    auto* entries = reinterpret_cast<Msg_S2C_ZoneEntryWire*>(body.data() + sizeof(header));
    auto& registry = m_owner.gatewayRegistry();
    for (size_t i = 0; i < zones.size(); ++i)
    {
        Msg_S2C_ZoneEntryWire& wire = entries[i];
        const ZoneInfoRow& row = zones[i];
        wire.zoneId = row.zoneId;
        wire.gameType = row.gameType;
        wire.enabled = row.enabled ? 1 : 0;
        copyToWire(wire.name, sizeof(wire.name), row.name.c_str());
        copyToWire(wire.ip, sizeof(wire.ip), row.ip.c_str());
        wire.superPort = row.superPort;

        ZoneInfoStore::fillGatewayLoadFields(row, m_owner.zoneInfoStore(),
                                             registry.countForZone(row.zoneId, row.gameType),
                                             wire.onlineCount, wire.gatewayCount, wire.loadLevel);
        const uint64_t nowMs = TimerMgr::NowMs();
        ZoneRuntimeRow runtime;
        const ZoneRuntimeRow* runtimePtr = nullptr;
        if (m_owner.zoneInfoStore().getRuntime(row.gameType, row.zoneId, runtime))
            runtimePtr = &runtime;
        const uint64_t runtimeAgeMs = runtimePtr && nowMs >= runtimePtr->lastReportMs
            ? (nowMs - runtimePtr->lastReportMs) : 0;
        LOG_DEBUG("区列表判定: zone=%u gameType=%u enabled=%u online=%u gateway=%u runtimeAlive=%u runtimeAgeMs=%llu loadLevel=%u",
                  row.zoneId, row.gameType, row.enabled ? 1 : 0, wire.onlineCount, wire.gatewayCount,
                  runtimePtr && runtimePtr->alive ? 1 : 0,
                  static_cast<unsigned long long>(runtimeAgeMs), wire.loadLevel);
        wire.reserved[0] = 0;
        wire.reserved[1] = 0;
    }

    m_owner.clientServer().SendMsg(connID, Msg_S2C_ZoneListRspHeader::kModule,
                                   Msg_S2C_ZoneListRspHeader::kSub,
                                   body.data(), static_cast<uint16_t>(bodyLen));
    LOG_INFO("已下发区列表: conn=%u count=%u filter=0x%02X",
             connID, header.count, gameTypeFilter);
}

void LoginAuthService::onClientLogin(ConnID connID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_LoginReq))
        return;
    const auto* req = reinterpret_cast<const Msg_C2S_LoginReq*>(data);

    Msg_S2C_LoginRsp loginRsp{};
    initClientMsg(loginRsp);
    loginRsp.userID = 0;
    loginRsp.accid = 0;
    loginRsp.loginToken[0] = '\0';
    loginRsp.tokenExpireMs = 0;

    if (m_owner.dbRequired() && !m_owner.db())
    {
        loginRsp.code = -1;
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "登录服不可用");
        sendClientWire(m_owner.clientServer(), connID, loginRsp);
        sendGatewayInfo(connID, -1, "无可用网关");
        return;
    }

    char accName[sizeof(req->account)];
    char password[sizeof(req->password)];
    copyToWire(accName, sizeof(accName), req->account);
    copyToWire(password, sizeof(password), req->password);

    MYSQL* db = m_owner.db();
    if (!db)
    {
        loginRsp.code = -1;
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "数据库不可用");
    }
    else
    {
        char escAccount[sizeof(accName) * 2 + 1];
        mysql_real_escape_string(db, escAccount, accName, strlen(accName));

        char sql[384];
        snprintf(sql, sizeof(sql),
                 "SELECT accid, password_hash, user_id, gamezone FROM GameUser "
                 "WHERE account='%s' LIMIT 1", escAccount);
        if (mysql_query(db, sql) != 0)
        {
            LOG_ERR("查询 GameUser 失败: %s", mysql_error(db));
            loginRsp.code = -1;
            copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "数据库错误");
        }
        else
        {
            MYSQL_RES* res = mysql_store_result(db);
            MYSQL_ROW row = res ? mysql_fetch_row(res) : nullptr;
            if (!row)
            {
                loginRsp.code = 1;
                copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "账号不存在");
            }
            else
            {
                const char* hash = row[1] ? row[1] : "";
                const uint32_t gameZone = row[3] ? static_cast<uint32_t>(strtoul(row[3], nullptr, 10)) : 0;
                if (!verifyPasswordBcrypt(password, hash))
                {
                    loginRsp.code = 1;
                    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "账号或密码错误");
                }
                else if (gameZone != 0 && gameZone != req->zoneId)
                {
                    loginRsp.code = 1;
                    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "区服不匹配");
                }
                else
                {
                    loginRsp.code = 0;
                    loginRsp.accid = row[0] ? static_cast<uint64_t>(strtoull(row[0], nullptr, 10)) : 0;
                    loginRsp.userID = row[2] ? static_cast<uint64_t>(strtoull(row[2], nullptr, 10)) : 0;
                    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "登录成功");

                    char token[65] = {};
                    if (!generateLoginToken(token, sizeof(token)))
                    {
                        loginRsp.code = -1;
                        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "登录票据生成失败");
                    }
                    else
                    {
                        char escToken[sizeof(token) * 2 + 1];
                        mysql_real_escape_string(db, escToken, token, strlen(token));
                        snprintf(sql, sizeof(sql),
                                 "DELETE FROM LoginSession WHERE accid=%llu AND zone_id=%u",
                                 static_cast<unsigned long long>(loginRsp.accid), req->zoneId);
                        mysql_query(db, sql);
                        snprintf(sql, sizeof(sql),
                                 "INSERT INTO LoginSession (token, accid, zone_id, game_type, expire_time) "
                                 "VALUES ('%s', %llu, %u, %u, DATE_ADD(NOW(), INTERVAL %u SECOND))",
                                 escToken,
                                 static_cast<unsigned long long>(loginRsp.accid),
                                 req->zoneId, req->gameType, LOGIN_TOKEN_TTL_SEC);
                        if (mysql_query(db, sql) != 0)
                        {
                            LOG_ERR("写入 LoginSession 失败: %s", mysql_error(db));
                            loginRsp.code = -1;
                            copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "会话写入失败");
                        }
                        else
                        {
                            copyToWire(loginRsp.loginToken, sizeof(loginRsp.loginToken), token);
                            loginRsp.tokenExpireMs =
                                TimerMgr::NowMs() + static_cast<uint64_t>(LOGIN_TOKEN_TTL_SEC) * 1000ULL;
                        }
                    }
                }
            }
            if (res)
                mysql_free_result(res);
        }
    }

    sendClientWire(m_owner.clientServer(), connID, loginRsp);
    if (loginRsp.code == 0)
    {
        LOG_INFO("账号登录成功: connID=%u zone=%u gameType=%u userID=%llu",
                 connID, req->zoneId, req->gameType,
                 static_cast<unsigned long long>(loginRsp.userID));
        logLoginFlow(LoginFlowPhase::ACCOUNT_LOGIN, loginRsp.accid, loginRsp.userID, connID, 0,
                     nullptr);
    }

    if (loginRsp.code == 0)
        sendGatewayInfo(connID, 0, "成功", req->zoneId, req->gameType);
    else
        sendGatewayInfo(connID, -1, "登录失败");
}

void LoginAuthService::sendGatewayInfo(ConnID connID, int32_t code, const char* msg,
                                       uint32_t zoneId, uint8_t gameType)
{
    Msg_S2C_GatewayInfo info{};
    initClientMsg(info);
    info.code = code;
    copyToWire(info.msg, sizeof(info.msg), msg);
    if (code == 0)
    {
        if (zoneId == 0)
        {
            info.code = -1;
            copyToWire(info.msg, sizeof(info.msg), "区服无效");
        }
        else
        {
            auto& zoneStore = m_owner.zoneInfoStore();
            auto& registry = m_owner.gatewayRegistry();

            ZoneInfoRow zone;
            if (!zoneStore.findZone(gameType, zoneId, zone))
            {
                info.code = -1;
                copyToWire(info.msg, sizeof(info.msg), "区服不存在");
            }
            else if (!zone.enabled)
            {
                info.code = -1;
                copyToWire(info.msg, sizeof(info.msg), "区服维护中");
            }
            else
            {
                uint32_t onlineCount = 0;
                uint8_t gatewayCount = 0;
                uint8_t loadLevel = 0;
                ZoneInfoStore::fillGatewayLoadFields(zone, zoneStore,
                                                     registry.countForZone(zoneId, gameType),
                                                     onlineCount, gatewayCount, loadLevel);
                if (loadLevel == static_cast<uint8_t>(ZoneLoadLevel::MAINTENANCE))
                {
                    info.code = -1;
                    copyToWire(info.msg, sizeof(info.msg), "区服不可用");
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
                        copyToWire(info.gatewayIP, sizeof(info.gatewayIP), clientGatewayIp.c_str());
                        info.gatewayPort = gw.port;
                        if (!zone.name.empty())
                            copyToWire(info.msg, sizeof(info.msg), zone.name.c_str());
                    }
                    else
                    {
                        info.code = -1;
                        copyToWire(info.msg, sizeof(info.msg), "无可用网关");
                    }
                }
            }
        }
    }
    sendClientWire(m_owner.clientServer(), connID, info);
    LOG_INFO("已下发网关信息: conn=%u code=%d ip=%s port=%u",
             connID, info.code, info.gatewayIP, info.gatewayPort);
}
