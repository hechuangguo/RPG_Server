/**
 * @file    GlobalHttpApi.h
 * @brief   GlobalServer HTTP 入站 API 路由与 JSON 响应生成
 *
 * 职责：
 *   - 将 HttpParser 解析后的 HttpRequest 路由到业务 action
 *   - 查询 MySQL（如 getUserList）或内存榜单，输出 JSON 正文（不含 HTTP 头）
 *
 * 线程：仅在 GlobalServer 主循环单线程内调用。
 */

#pragma once

#include "../sdk/http/HttpMessage.h"

#include <mysql/mysql.h>
#include <string>
#include <vector>

struct RankEntry;

/**
 * @brief HTTP API 处理结果（由 HttpCodec::buildJsonResponse 封装为完整报文）
 */
struct HttpApiResponse
{
    int         status = 200;     /**< HTTP 状态码 */
    const char* reason = "OK";    /**< 原因短语 */
    std::string jsonBody;         /**< JSON 正文 */
};

/**
 * @brief GlobalServer HTTP API 静态路由
 */
class GlobalHttpApi
{
public:
    /**
     * @brief 根据请求路径与方法分发 API
     * @param req  已解析的 HTTP 请求
     * @param db   MySQL 句柄；未配置时为 nullptr
     * @param rank 内存排行榜快照（/rank 使用）
     * @return 状态码与 JSON 正文
     */
    static HttpApiResponse dispatch(const HttpRequest& req, MYSQL* db,
                                    const std::vector<RankEntry>& rank);
};
