/**
 * @file    RecordCharService.cpp
 * @brief   RecordServer 角色列表/创角/token 校验实现
 */

#include "RecordCharService.h"
#include "../sdk/log/Logger.h"
#include "../sdk/util/LoginEnterErrorCode.h"
#include "../sdk/util/LoginSpawnConfig.h"
#include "../sdk/util/LoginFlowLog.h"
#include "../sdk/util/WireStringUtil.h"

#include <cstdio>
#include <cstring>
#include <limits>

namespace
{

bool isValidRoleName(const char* name)
{
    if (!name || name[0] == '\0')
        return false;
    const size_t n = strlen(name);
    if (n < 2 || n > 16)
        return false;
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok)
            return false;
    }
    return true;
}

} // namespace

void RecordCharService::listCharacters(MYSQL* db, const Msg_REC_ListCharactersReq& req,
                                       Msg_REC_ListCharactersRspHeader& hdr,
                                       std::vector<Msg_REC_CharacterEntryWire>& entries)
{
    hdr = {};
    hdr.gatewayConnID = req.gatewayConnID;
    entries.clear();
    if (!db)
    {
        hdr.code = -1;
        return;
    }

    char sql[384];
    snprintf(sql, sizeof(sql),
             "SELECT user_id, name, level, vocation, sex FROM CharBase "
             "WHERE accid=%llu AND gamezone=%u ORDER BY user_id",
             static_cast<unsigned long long>(req.accid), req.zoneId);

    if (mysql_query(db, sql) != 0)
    {
        LOG_ERR("查询角色列表失败: %s", mysql_error(db));
        hdr.code = -1;
        return;
    }

    MYSQL_RES* res = mysql_store_result(db);
    if (res)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr)
        {
            Msg_REC_CharacterEntryWire e{};
            e.userID = row[0] ? static_cast<uint64_t>(strtoull(row[0], nullptr, 10)) : 0;
            if (row[1])
                copyToWire(e.name, sizeof(e.name), row[1]);
            e.level = row[2] ? static_cast<uint32_t>(strtoul(row[2], nullptr, 10)) : 1;
            e.vocation = row[3] ? static_cast<uint8_t>(strtoul(row[3], nullptr, 10)) : 0;
            e.sex = row[4] ? static_cast<uint8_t>(strtoul(row[4], nullptr, 10)) : 0;
            entries.push_back(e);
        }
        mysql_free_result(res);
    }

    if (entries.size() > std::numeric_limits<uint16_t>::max())
    {
        LOG_ERR("角色列表条数超出协议上限: accid=%llu zone=%u count=%zu",
                static_cast<unsigned long long>(req.accid), req.zoneId, entries.size());
        entries.clear();
        hdr.code = -1;
        hdr.count = 0;
        logLoginFlow(LoginFlowPhase::CHAR_LIST, req.accid, 0, req.gatewayConnID, hdr.code,
                     "角色列表条数超限");
        return;
    }

    hdr.code = 0;
    hdr.count = static_cast<uint16_t>(entries.size());
    logLoginFlow(LoginFlowPhase::CHAR_LIST, req.accid, 0, req.gatewayConnID, 0, nullptr);
}

void RecordCharService::createCharacter(MYSQL* db, const Msg_REC_CreateCharacterReq& req,
                                        Msg_REC_CreateCharacterRsp& rsp)
{
    rsp = {};
    rsp.gatewayConnID = req.gatewayConnID;
    if (!db)
    {
        rsp.code = static_cast<int32_t>(CreateCharacterError::SYSTEM_ERROR);
        logLoginFlow(LoginFlowPhase::CHAR_CREATE, req.accid, 0, req.gatewayConnID, rsp.code,
                     "数据库不可用");
        return;
    }

    char roleName[sizeof(req.name)];
    copyToWire(roleName, sizeof(roleName), req.name);
    if (!isValidRoleName(roleName))
    {
        rsp.code = static_cast<int32_t>(CreateCharacterError::INVALID_NAME);
        logLoginFlow(LoginFlowPhase::CHAR_CREATE, req.accid, 0, req.gatewayConnID, rsp.code,
                     "角色名非法");
        return;
    }
    if (req.vocation > MAX_VOCATION_ID || req.sex > MAX_SEX_ID)
    {
        rsp.code = static_cast<int32_t>(CreateCharacterError::INVALID_VOCATION);
        logLoginFlow(LoginFlowPhase::CHAR_CREATE, req.accid, 0, req.gatewayConnID, rsp.code,
                     "职业或性别非法");
        return;
    }

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT COUNT(*) FROM CharBase WHERE accid=%llu AND gamezone=%u",
             static_cast<unsigned long long>(req.accid), req.zoneId);
    if (mysql_query(db, sql) != 0)
    {
        rsp.code = static_cast<int32_t>(CreateCharacterError::SYSTEM_ERROR);
        logLoginFlow(LoginFlowPhase::CHAR_CREATE, req.accid, 0, req.gatewayConnID, rsp.code,
                     "查询角色数量失败");
        return;
    }
    MYSQL_RES* cntRes = mysql_store_result(db);
    MYSQL_ROW cntRow = cntRes ? mysql_fetch_row(cntRes) : nullptr;
    const uint32_t charCount = cntRow && cntRow[0] ? static_cast<uint32_t>(strtoul(cntRow[0], nullptr, 10)) : 0;
    if (cntRes)
        mysql_free_result(cntRes);
    if (charCount >= MAX_CHARACTERS_PER_ACCOUNT)
    {
        rsp.code = static_cast<int32_t>(CreateCharacterError::LIMIT_REACHED);
        logLoginFlow(LoginFlowPhase::CHAR_CREATE, req.accid, 0, req.gatewayConnID, rsp.code,
                     "达到角色上限");
        return;
    }

    char escName[sizeof(roleName) * 2 + 1];
    mysql_real_escape_string(db, escName, roleName, strlen(roleName));
    snprintf(sql, sizeof(sql),
             "INSERT INTO CharBase (accid, gamezone, name, vocation, sex, map_id, pos_x, pos_y, pos_z) "
             "VALUES (%llu, %u, '%s', %u, %u, %u, %.2f, %.2f, %.2f)",
             static_cast<unsigned long long>(req.accid), req.zoneId, escName,
             req.vocation, req.sex,
             DEFAULT_NEWBIE_MAP_ID,
             DEFAULT_NEWBIE_SPAWN_X, DEFAULT_NEWBIE_SPAWN_Y, DEFAULT_NEWBIE_SPAWN_Z);
    if (mysql_query(db, sql) != 0)
    {
        if (mysql_errno(db) == 1062)
        {
            rsp.code = static_cast<int32_t>(CreateCharacterError::NAME_EXISTS);
            logLoginFlow(LoginFlowPhase::CHAR_CREATE, req.accid, 0, req.gatewayConnID, rsp.code,
                         "角色名重复");
        }
        else
        {
            LOG_ERR("创角失败: %s", mysql_error(db));
            rsp.code = static_cast<int32_t>(CreateCharacterError::SYSTEM_ERROR);
            logLoginFlow(LoginFlowPhase::CHAR_CREATE, req.accid, 0, req.gatewayConnID, rsp.code,
                         "写入角色失败");
        }
        return;
    }

    rsp.userID = static_cast<uint64_t>(mysql_insert_id(db));
    rsp.code = static_cast<int32_t>(CreateCharacterError::OK);
    LOG_INFO("创角成功: accid=%llu userID=%llu name=%s zone=%u",
             static_cast<unsigned long long>(req.accid),
             static_cast<unsigned long long>(rsp.userID), roleName, req.zoneId);
    logLoginFlow(LoginFlowPhase::CHAR_CREATE, req.accid, rsp.userID, req.gatewayConnID, 0,
                 roleName);
}
