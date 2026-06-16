/**
 * @file    LoginAuthService.cpp
 * @brief  LoginAuthService 实现
 */

#include "LoginAuthService.h"
#include "LoginServer.h"
#include "../Common/ClientMsg.h"
#include "../sdk/log/Logger.h"
#include "../sdk/util/PasswordUtil.h"
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

        ZoneRuntimeRow runtime;
        const ZoneRuntimeRow* runtimePtr = nullptr;
        if (m_owner.zoneInfoStore().getRuntime(row.gameType, row.zoneId, runtime))
            runtimePtr = &runtime;

        const size_t gatewayCount = registry.countForZone(row.zoneId, row.gameType);
        wire.onlineCount = runtimePtr ? runtimePtr->onlineCount : 0;
        wire.gatewayCount = static_cast<uint8_t>(
            gatewayCount > 255 ? 255 : gatewayCount);
        wire.loadLevel = ZoneInfoStore::computeLoadLevel(row, runtimePtr, gatewayCount);
        wire.reserved[0] = 0;
        wire.reserved[1] = 0;
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
    char password[sizeof(req->password)];
    copyToWire(accName, sizeof(accName), req->account);
    copyToWire(password, sizeof(password), req->password);

    MYSQL* db = m_owner.db();
    if (!db)
    {
        loginRsp.code = -1;
        copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Database unavailable");
    }
    else
    {
        char escAccount[sizeof(accName) * 2 + 1];
        mysql_real_escape_string(db, escAccount, accName, strlen(accName));

        char sql[384];
        snprintf(sql, sizeof(sql),
                 "SELECT password_hash, user_id, gamezone FROM GameUser "
                 "WHERE account='%s' LIMIT 1", escAccount);
        if (mysql_query(db, sql) != 0)
        {
            LOG_ERR("LoginServer query GameUser failed: %s", mysql_error(db));
            loginRsp.code = -1;
            copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Database error");
        }
        else
        {
            MYSQL_RES* res = mysql_store_result(db);
            MYSQL_ROW row = res ? mysql_fetch_row(res) : nullptr;
            if (!row)
            {
                loginRsp.code = 1;
                copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Account not found");
            }
            else
            {
                const char* hash = row[0] ? row[0] : "";
                const uint32_t gameZone = row[2] ? static_cast<uint32_t>(strtoul(row[2], nullptr, 10)) : 0;
                if (!verifyPasswordBcrypt(password, hash))
                {
                    loginRsp.code = 1;
                    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Invalid account or password");
                }
                else if (gameZone != 0 && gameZone != req->zoneId)
                {
                    loginRsp.code = 1;
                    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Zone mismatch");
                }
                else
                {
                    loginRsp.code = 0;
                    loginRsp.userID = row[1] ? static_cast<uint64_t>(strtoull(row[1], nullptr, 10)) : 0;
                    copyToWire(loginRsp.msg, sizeof(loginRsp.msg), "Login OK");
                }
            }
            if (res)
                mysql_free_result(res);
        }
    }

    m_owner.clientServer().SendMsg(connID, (uint16_t)ClientMsgID::S2C_LOGIN_RSP,
                                   reinterpret_cast<char*>(&loginRsp), sizeof(loginRsp));

    if (loginRsp.code == 0)
        sendGatewayInfo(connID, 0, "OK", req->zoneId, req->gameType);
    else
        sendGatewayInfo(connID, -1, "Login failed");
}

void LoginAuthService::sendGatewayInfo(ConnID connID, int32_t code, const char* msg,
                                       uint32_t zoneId, uint8_t gameType)
{
    Msg_S2C_GatewayInfo info{};
    info.code = code;
    copyToWire(info.msg, sizeof(info.msg), msg);
    if (code == 0)
    {
        if (zoneId == 0)
        {
            info.code = -1;
            copyToWire(info.msg, sizeof(info.msg), "Invalid zone");
        }
        else
        {
            auto& zoneStore = m_owner.zoneInfoStore();
            auto& registry = m_owner.gatewayRegistry();

            ZoneInfoRow zone;
            if (!zoneStore.findZone(gameType, zoneId, zone))
            {
                info.code = -1;
                copyToWire(info.msg, sizeof(info.msg), "Zone not found");
            }
            else if (!zone.enabled)
            {
                info.code = -1;
                copyToWire(info.msg, sizeof(info.msg), "Zone maintenance");
            }
            else
            {
                ZoneRuntimeRow runtime;
                const ZoneRuntimeRow* runtimePtr = nullptr;
                if (zoneStore.getRuntime(gameType, zoneId, runtime))
                    runtimePtr = &runtime;

                const size_t gatewayCount = registry.countForZone(zoneId, gameType);
                const uint8_t loadLevel =
                    ZoneInfoStore::computeLoadLevel(zone, runtimePtr, gatewayCount);
                if (loadLevel == static_cast<uint8_t>(ZoneLoadLevel::MAINTENANCE))
                {
                    info.code = -1;
                    copyToWire(info.msg, sizeof(info.msg), "Zone unavailable");
                }
                else
                {
                    LoginGatewayEntry gw;
                    if (registry.pickByZone(zoneId, gameType, gw))
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
            }
        }
    }
    m_owner.clientServer().SendMsg(connID, (uint16_t)ClientMsgID::S2C_GATEWAY_INFO,
                                   reinterpret_cast<char*>(&info), sizeof(info));
    LOG_INFO("Sent gateway info: conn=%u code=%d ip=%s port=%u",
             connID, info.code, info.gatewayIP, info.gatewayPort);
}
