/**
 * @file    HttpMessage.h
 * @brief   HTTP/1.1 简易消息结构体定义
 *
 * 职责：
 *   - 承载 HttpParser 解析后的请求/响应字段
 *   - 供 GlobalHttpServer / GlobalHttpClient / GlobalHttpApi 使用
 *
 * 限制：仅支持 HTTP/1.1 明文；body 依赖 Content-Length，不支持 chunked/upgrade。
 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

/**
 * @brief HTTP 请求（解析结果）
 */
struct HttpRequest
{
    std::string method; /**< 方法，如 GET、POST */
    std::string path;   /**< 路径，可含 query（如 /getUserList?limit=10） */
    std::string version; /**< 协议版本，如 HTTP/1.1 */
    std::unordered_map<std::string, std::string> headers; /**< 头字段（键为小写） */
    std::string body;   /**< 正文（由 Content-Length 界定） */
};

/**
 * @brief HTTP 响应（解析结果）
 */
struct HttpResponse
{
    int         status = 0;   /**< 状态码，如 200、404 */
    std::string reason;       /**< 原因短语，如 OK、Not Found */
    std::string version;      /**< 协议版本 */
    std::unordered_map<std::string, std::string> headers; /**< 头字段（键为小写） */
    std::string body;         /**< 正文 */
};
