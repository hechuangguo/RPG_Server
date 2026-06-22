/**
 * @file    RelationStore.cpp
 * @brief   Relation 表 SQL 与线格式实现
 */

#include "RelationStore.h"
#include "../sdk/log/Logger.h"

#include <cinttypes>
#include <mysql/mysql.h>
#include <sstream>

namespace {

std::string blobToSqlHex(const std::vector<uint8_t>& data)
{
    if (data.empty()) return "x''";
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(3 + data.size() * 2);
    out += "x'";
    for (uint8_t b : data)
    {
        out += kHex[b >> 4];
        out += kHex[b & 0x0F];
    }
    out += '\'';
    return out;
}

std::string escapeSqlString(MYSQL* db, const std::string& value)
{
    if (!db)
        return value;
    std::string out;
    out.resize(value.size() * 2 + 1);
    const unsigned long len = mysql_real_escape_string(
        db, out.data(), value.c_str(), static_cast<unsigned long>(value.size()));
    out.resize(len);
    return out;
}

} // namespace

RelationStore::RelationStore(MYSQL* db)
    : m_db(db)
{
}

bool RelationStore::preloadAll(std::vector<RelationRow>& out) const
{
    out.clear();
    if (!m_db) return false;

    const char* sql =
        "SELECT user_id,friends_json,blacklist_json,guild_id,team_id,`binary` FROM Relation";
    if (mysql_query(m_db, sql) != 0)
    {
        LOG_ERR("关系存储 preloadAll SQL 失败: %s", mysql_error(m_db));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(m_db);
    if (!res)
    {
        LOG_ERR("关系存储 preloadAll store_result 失败: %s", mysql_error(m_db));
        return false;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        unsigned long* lengths = mysql_fetch_lengths(res);
        RelationRow r;
        r.userID = row[0] ? strtoull(row[0], nullptr, 10) : 0;
        r.friendsJson   = row[1] ? row[1] : "";
        r.blacklistJson = row[2] ? row[2] : "";
        r.guildId = row[3] ? strtoull(row[3], nullptr, 10) : 0;
        r.teamId  = row[4] ? static_cast<uint32_t>(strtoul(row[4], nullptr, 10)) : 0;
        const unsigned long binaryLen = lengths ? lengths[5] : 0;
        if (row[5] && binaryLen > 0)
            r.binary.assign(reinterpret_cast<const uint8_t*>(row[5]),
                            reinterpret_cast<const uint8_t*>(row[5]) + binaryLen);
        out.push_back(std::move(r));
    }
    mysql_free_result(res);
    return true;
}

bool RelationStore::loadOne(uint64_t userID, RelationRow& out) const
{
    if (!m_db) return false;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT friends_json,blacklist_json,guild_id,team_id,`binary`"
             " FROM Relation WHERE user_id=%" PRIu64 " LIMIT 1", userID);

    if (mysql_query(m_db, sql) != 0)
    {
        LOG_ERR("关系存储 loadOne SQL 失败: %s", mysql_error(m_db));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(m_db);
    MYSQL_ROW  row = res ? mysql_fetch_row(res) : nullptr;
    unsigned long* lengths = res && row ? mysql_fetch_lengths(res) : nullptr;

    out.userID = userID;
    if (row)
    {
        out.friendsJson   = row[0] ? row[0] : "";
        out.blacklistJson = row[1] ? row[1] : "";
        out.guildId = row[2] ? strtoull(row[2], nullptr, 10) : 0;
        out.teamId  = row[3] ? static_cast<uint32_t>(strtoul(row[3], nullptr, 10)) : 0;
        const unsigned long binaryLen = lengths ? lengths[4] : 0;
        out.binary.clear();
        if (row[4] && binaryLen > 0)
            out.binary.assign(reinterpret_cast<const uint8_t*>(row[4]),
                             reinterpret_cast<const uint8_t*>(row[4]) + binaryLen);
    }
    else
    {
        char ins[320];
        snprintf(ins, sizeof(ins),
                 "INSERT INTO Relation (user_id,friends_json,blacklist_json,guild_id,team_id,`binary`)"
                 " VALUES (%" PRIu64 ",'','',0,0,x'')", userID);
        if (mysql_query(m_db, ins) != 0)
            LOG_WARN("关系存储 loadOne 自动插入失败: %s", mysql_error(m_db));
        out.friendsJson.clear();
        out.blacklistJson.clear();
        out.guildId = 0;
        out.teamId  = 0;
        out.binary.clear();
    }

    if (res) mysql_free_result(res);
    return true;
}

bool RelationStore::saveOne(const RelationRow& row) const
{
    if (!m_db) return false;

    const std::string binaryLit = blobToSqlHex(row.binary);
    const std::string friendsEsc = escapeSqlString(m_db, row.friendsJson);
    const std::string blacklistEsc = escapeSqlString(m_db, row.blacklistJson);
    std::ostringstream sql;
    sql << "INSERT INTO Relation (user_id,friends_json,blacklist_json,guild_id,team_id,`binary`)"
        << " VALUES (" << row.userID << ",'" << friendsEsc << "','" << blacklistEsc << "',"
        << row.guildId << "," << row.teamId << "," << binaryLit << ")"
        << " ON DUPLICATE KEY UPDATE friends_json=VALUES(friends_json),"
        << " blacklist_json=VALUES(blacklist_json),"
        << " guild_id=VALUES(guild_id), team_id=VALUES(team_id),"
        << " `binary`=VALUES(`binary`)";

    const std::string q = sql.str();
    if (mysql_query(m_db, q.c_str()) != 0)
    {
        LOG_ERR("关系存储 saveOne SQL 失败: %s", mysql_error(m_db));
        return false;
    }
    return true;
}

void RelationStore::encodePreloadRsp(int32_t code, const std::vector<RelationRow>& rows,
                                     std::vector<char>& out)
{
    out.resize(sizeof(Msg_REC_RelationPreloadRsp));
    auto* hdr = reinterpret_cast<Msg_REC_RelationPreloadRsp*>(out.data());
    hdr->code  = code;
    hdr->count = static_cast<uint32_t>(rows.size());
    for (const RelationRow& row : rows)
        RelationWireUtil::appendRow(row, out);
}

void RelationStore::encodeLoadRsp(int32_t code, uint64_t userID, const RelationRow* row,
                                  std::vector<char>& out)
{
    out.resize(sizeof(Msg_REC_RelationLoadRsp));
    auto* hdr = reinterpret_cast<Msg_REC_RelationLoadRsp*>(out.data());
    hdr->code   = code;
    hdr->userID = userID;
    if (code == 0 && row)
        RelationWireUtil::appendRow(*row, out);
}

bool RelationStore::decodeSaveReq(const char* data, uint16_t len, RelationRow& out)
{
    size_t offset = 0;
    return RelationWireUtil::parseRow(data, len, offset, out);
}

bool RelationStore::decodeRows(const char* data, uint16_t len, size_t headerSize,
                               std::vector<RelationRow>& out)
{
    return RelationWireUtil::parseAllRows(data, len, headerSize, out);
}
