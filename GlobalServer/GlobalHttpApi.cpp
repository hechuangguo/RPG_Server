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

HttpApiResponse handleGetUserList(MYSQL* /*db*/, const std::string& /*fullPath*/)
{
    return makeError(503, "Service Unavailable", 503,
                     "getUserList 暂未实现，全区数据请使用 rpg_global");
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
