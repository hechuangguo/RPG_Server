/**
 * @file    GlobalHttpApi.cpp
 * @brief   GlobalServer HTTP API 路由与 JSON 生成实现
 */

#include "GlobalHttpApi.h"
#include "GlobalServer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
constexpr int DEFAULT_USER_LIMIT = 50;
constexpr int MAX_USER_LIMIT     = 200;

/** @brief JSON 字符串转义（仅处理引号、反斜杠与控制字符） */
std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
                out += ' ';
            else
                out += c;
            break;
        }
    }
    return out;
}

/** @brief 分离 path 与 query（path 含 ? 时） */
std::string pathOnly(const std::string& path)
{
    const size_t q = path.find('?');
    if (q == std::string::npos)
        return path;
    return path.substr(0, q);
}

/** @brief 从 query 解析 limit 参数 */
int parseLimitQuery(const std::string& path, int fallback, int maxVal)
{
    const size_t q = path.find('?');
    if (q == std::string::npos)
        return fallback;
    const std::string query = path.substr(q + 1);
    size_t pos = 0;
    while (pos < query.size())
    {
        size_t amp = query.find('&', pos);
        if (amp == std::string::npos)
            amp = query.size();
        const std::string pair = query.substr(pos, amp - pos);
        const size_t eq = pair.find('=');
        if (eq != std::string::npos)
        {
            const std::string key = pair.substr(0, eq);
            if (key == "limit")
            {
                int v = std::atoi(pair.substr(eq + 1).c_str());
                if (v <= 0)
                    return fallback;
                return v > maxVal ? maxVal : v;
            }
        }
        pos = amp + 1;
    }
    return fallback;
}

HttpApiResponse makeError(int httpStatus, const char* reason, int code, const char* message)
{
    HttpApiResponse rsp;
    rsp.status = httpStatus;
    rsp.reason = reason;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "{\"code\":%d,\"message\":\"%s\"}", code, message);
    rsp.jsonBody = buf;
    return rsp;
}

HttpApiResponse handleHealth()
{
    HttpApiResponse rsp;
    rsp.jsonBody = "{\"code\":0,\"action\":\"health\",\"status\":\"ok\"}";
    return rsp;
}

HttpApiResponse handleRank(const std::vector<RankEntry>& rank)
{
    HttpApiResponse rsp;
    std::string body = "{\"code\":0,\"action\":\"rank\",\"count\":";
    body += std::to_string(rank.size());
    body += ",\"rank\":[";
    for (size_t i = 0; i < rank.size(); ++i)
    {
        if (i > 0)
            body += ',';
        char item[160];
        std::snprintf(item, sizeof(item),
                      "{\"userId\":%llu,\"name\":\"%s\",\"value\":%u}",
                      static_cast<unsigned long long>(rank[i].userID),
                      jsonEscape(rank[i].name).c_str(),
                      rank[i].value);
        body += item;
    }
    body += "]}";
    rsp.jsonBody = std::move(body);
    return rsp;
}

HttpApiResponse handleGetUserList(MYSQL* db, const std::string& fullPath)
{
    if (!db)
        return makeError(503, "Service Unavailable", 503, "database not available");

    const int limit = parseLimitQuery(fullPath, DEFAULT_USER_LIMIT, MAX_USER_LIMIT);
    char sql[192];
    std::snprintf(sql, sizeof(sql),
                  "SELECT user_id, name, level, vocation FROM CharBase "
                  "ORDER BY user_id LIMIT %d",
                  limit);

    if (mysql_query(db, sql) != 0)
        return makeError(500, "Internal Server Error", 500, "query failed");

    MYSQL_RES* res = mysql_store_result(db);
    if (!res)
        return makeError(500, "Internal Server Error", 500, "query failed");

    std::string users = "[";
    bool first = true;
    MYSQL_ROW row;
    int count = 0;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        if (!row[0] || !row[1])
            continue;
        if (!first)
            users += ',';
        first = false;
        ++count;
        const unsigned long long userId = std::strtoull(row[0], nullptr, 10);
        const int level = row[2] ? std::atoi(row[2]) : 0;
        const int vocation = row[3] ? std::atoi(row[3]) : 0;
        char item[256];
        std::snprintf(item, sizeof(item),
                      "{\"userId\":%llu,\"name\":\"%s\",\"level\":%d,\"vocation\":%d}",
                      userId, jsonEscape(row[1]).c_str(), level, vocation);
        users += item;
    }
    mysql_free_result(res);
    users += ']';

    HttpApiResponse rsp;
    rsp.jsonBody = "{\"code\":0,\"action\":\"getUserList\",\"count\":" + std::to_string(count)
                   + ",\"users\":" + users + "}";
    return rsp;
}
}  // namespace

HttpApiResponse GlobalHttpApi::dispatch(const HttpRequest& req, MYSQL* db,
                                         const std::vector<RankEntry>& rank)
{
    if (req.method != "GET")
        return makeError(405, "Method Not Allowed", 405, "method not allowed");

    const std::string path = pathOnly(req.path);
    if (path == "/health" || path == "/api/health")
        return handleHealth();
    if (path == "/rank" || path == "/api/rank")
        return handleRank(rank);
    if (path == "/getUserList" || path == "/api/getUserList")
        return handleGetUserList(db, req.path);

    return makeError(404, "Not Found", 404, "unknown action");
}
