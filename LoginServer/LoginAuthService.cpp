/**
 * @file    LoginAuthService.cpp
 * @brief  LoginAuthService 实现
 */

#include "LoginAuthService.h"
#include "LoginServer.h"
#include "../Common/ClientMsg.h"
#include "../sdk/log/Logger.h"
#include "../sdk/util/WireStringUtil.h"

#include <cstring>
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
    if (zones.size() > MAX_ZONE_LIST_ENTRIES)
    {
        header.code = -1;
        header.count = 0;
        LOG_ERR("Zone list too large: %zu entries (max %u)", zones.size(), MAX_ZONE_LIST_ENTRIES);
        m_owner.clientServer().SendMsg(connID, (uint16_t)ClientMsgID::S2C_ZONE_LIST_RSP,
                                       reinterpret_cast<char*>(&header), sizeof(header));
        return;
    }

    header.code = 0;
    header.count = static_cast<uint16_t>(zones.size());

    const size_t bodyLen = sizeof(header) + header.count * sizeof(Msg_S2C_ZoneEntryWire);
    std::vector<char> body(bodyLen);
    std::memcpy(body.data(), &header, sizeof(header));

    auto* entries = reinterpret_cast<Msg_S2C_ZoneEntryWire*>(body.data() + sizeof(header));
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
    }

    m_owner.clientServer().SendMsg(connID, (uint16_t)ClientMsgID::S2C_ZONE_LIST_RSP,
                                   body.data(), static_cast<uint16_t>(bodyLen));
    LOG_INFO("Sent zone list: conn=%u count=%u filter=0x%02X",
             connID, header.count, gameTypeFilter);
}

void LoginAuthService::onClientLogin(ConnID connID, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_C2S_LoginReq))
        return;
    const auto* req = reinterpret_cast<const Msg_C2S_LoginReq*>(data);

    Msg_S2C_LoginRsp loginRsp{};
    loginRsp.userID = 0;

    if (m_owner.dbRequired() && !m_owner.db())
    {
        loginRsp.code = -1;
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Login service unavailable");
        m_owner.clientServer().SendMsg(connID, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                                       reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));
        sendGatewayInfo(connID, -1, "No gateway");
        return;
    }

    char accName[sizeof(req->account)];
    copyToWire(accName, sizeof(accName), req->account);

    MYSQL* db = m_owner.db();
    if (db)
    {
        char escName[sizeof(accName) * 2 + 1];
        mysql_real_escape_string(db, escName, accName, strlen(accName));
        char sql[256];
        snprintf(sql, sizeof(sql),
                 "SELECT user_id FROM CharBase WHERE name='%s' LIMIT 1", escName);

        if (mysql_query(db, sql) != 0)
        {
            LOG_ERR("LoginServer MySQL query failed: %s", mysql_error(db));
            loginRsp.code = -1;
            copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Database error");
        }
        else
        {
            MYSQL_RES* res = mysql_store_result(db);
            MYSQL_ROW row = res ? mysql_fetch_row(res) : nullptr;
            if (row)
            {
                loginRsp.code = 0;
                loginRsp.userID = static_cast<uint64_t>(strtoull(row[0], nullptr, 10));
                copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Login OK");
            }
            else
            {
                snprintf(sql, sizeof(sql),
                         "INSERT INTO CharBase (name) VALUES ('%s')", escName);
                if (mysql_query(db, sql) != 0)
                {
                    LOG_ERR("LoginServer auto-create failed: %s", mysql_error(db));
                    loginRsp.code = -1;
                    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Create account failed");
                }
                else
                {
                    loginRsp.code = 0;
                    loginRsp.userID = static_cast<uint64_t>(mysql_insert_id(db));
                    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Login OK");
                    LOG_INFO("LoginServer auto-create: name=%s userID=%llu",
                             accName, static_cast<unsigned long long>(loginRsp.userID));
                }
            }
            if (res)
                mysql_free_result(res);
        }
    }
    else
    {
        loginRsp.code = 0;
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Login OK (no DB)");
    }

    m_owner.clientServer().SendMsg(connID, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                                   reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));

    if (loginRsp.code == 0)
        sendGatewayInfo(connID, 0, "OK");
    else
        sendGatewayInfo(connID, -1, "Login failed");
}

void LoginAuthService::sendGatewayInfo(ConnID connID, int32_t code, const char* msg)
{
    Msg_S2C_GatewayInfo info{};
    info.code = code;
    copyToWire(info.msg, sizeof(info.msg), msg);
    if (code == 0)
    {
        LoginGatewayEntry gw;
        ZoneInfoRow zone;
        auto& zoneStore = m_owner.zoneInfoStore();
        auto& registry = m_owner.gatewayRegistry();

        if (zoneStore.size() > 0)
        {
            if (zoneStore.pickRoundRobin(zone) &&
                registry.pickByServerId(zone.zoneId, gw))
            {
                copyToWire(info.gatewayIP, sizeof(info.gatewayIP), gw.ip.c_str());
                info.gatewayPort = gw.port;
                if (!zone.name.empty())
                    copyToWire(info.msg, sizeof(info.msg), zone.name.c_str());
            }
            else
            {
                info.code = -1;
                copyToWire(info.msg, sizeof(info.msg), "No gateway available");
            }
        }
        else if (registry.pickRoundRobin(gw))
        {
            copyToWire(info.gatewayIP, sizeof(info.gatewayIP), gw.ip.c_str());
            info.gatewayPort = gw.port;
        }
        else
        {
            info.code = -1;
            copyToWire(info.msg, sizeof(info.msg), "No gateway available");
        }
    }
    m_owner.clientServer().SendMsg(connID, (uint16_t)ClientMsgID::S2C_GATEWAY_INFO,
                                   reinterpret_cast<char*>(&info), sizeof(info));
    LOG_INFO("Sent gateway info: conn=%u code=%d ip=%s port=%u",
             connID, info.code, info.gatewayIP, info.gatewayPort);
}
