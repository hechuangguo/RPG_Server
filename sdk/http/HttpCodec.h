/**
 * @file    HttpCodec.h
 * @brief   HTTP/1.1 明文报文构建（请求与响应）
 *
 * 供 GlobalHttpServer 回写响应、GlobalHttpClient 发送 GET 使用。
 * 不解析入站报文（解析见 HttpParser）。
 */

#pragma once

#include <cstdio>
#include <string>

namespace HttpCodec {

/**
 * @brief 构建 HTTP/1.1 响应（text/plain）
 * @param status 状态码
 * @param reason 原因短语
 * @param body   响应正文
 * @return 完整 HTTP 报文字节流
 */
inline std::string buildResponse(int status, const char* reason, const std::string& body)
{
    std::string out;
    out.reserve(256 + body.size());
    char line[128];
    std::snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n", status, reason);
    out += line;
    out += "Content-Type: text/plain; charset=utf-8\r\n";
    out += "Connection: close\r\n";
    std::snprintf(line, sizeof(line), "Content-Length: %zu\r\n\r\n", body.size());
    out += line;
    out += body;
    return out;
}

/**
 * @brief 构建 HTTP/1.1 JSON 响应（application/json）
 * @param status 状态码
 * @param reason 原因短语
 * @param jsonBody JSON 正文（调用方保证合法）
 * @return 完整 HTTP 报文字节流
 */
inline std::string buildJsonResponse(int status, const char* reason,
                                     const std::string& jsonBody)
{
    std::string out;
    out.reserve(256 + jsonBody.size());
    char line[128];
    std::snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n", status, reason);
    out += line;
    out += "Content-Type: application/json; charset=utf-8\r\n";
    out += "Connection: close\r\n";
    std::snprintf(line, sizeof(line), "Content-Length: %zu\r\n\r\n", jsonBody.size());
    out += line;
    out += jsonBody;
    return out;
}

/**
 * @brief 构建 HTTP/1.1 GET 请求
 * @param path 请求路径（如 /health）
 * @param host Host 头
 * @return 完整 HTTP 请求字节流
 */
inline std::string buildGetRequest(const char* path, const char* host)
{
    std::string out;
    out.reserve(128);
    out += "GET ";
    out += path;
    out += " HTTP/1.1\r\nHost: ";
    out += host;
    out += "\r\nConnection: close\r\n\r\n";
    return out;
}

}  // namespace HttpCodec
