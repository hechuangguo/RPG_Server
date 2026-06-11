/**
 * @file    HttpParser.h
 * @brief   HTTP/1.1 增量报文解析器
 *
 * 从 TCP 接收缓冲中解析完整请求或响应；数据不足时返回 NEED_MORE，
 * 由调用方继续读 socket 后重试。
 */

#pragma once

#include "HttpMessage.h"

#include <cstddef>
#include <string>

/**
 * @brief 解析结果
 */
enum class HttpParseResult
{
    NEED_MORE, /**< 头或 body 未收齐，需继续读 */
    OK,        /**< 已成功解析一条完整报文 */
    ERROR      /**< 起始行或头格式非法 */
};

/**
 * @brief HTTP/1.1 报文解析器（静态工具，无状态）
 */
class HttpParser
{
public:
    /**
     * @brief 从缓冲区解析 HTTP 请求
     * @param data     输入数据起始
     * @param len      可用字节数
     * @param consumed [out] 成功时本帧消耗的字节数
     * @param out      [out] 解析结果
     * @return 解析状态
     */
    static HttpParseResult parseRequest(const char* data, size_t len, size_t& consumed,
                                        HttpRequest& out);

    /**
     * @brief 从缓冲区解析 HTTP 响应
     * @param data     输入数据起始
     * @param len      可用字节数
     * @param consumed [out] 成功时本帧消耗的字节数
     * @param out      [out] 解析结果
     * @return 解析状态
     */
    static HttpParseResult parseResponse(const char* data, size_t len, size_t& consumed,
                                         HttpResponse& out);

private:
    static HttpParseResult parseMessage(const char* data, size_t len, size_t& consumed,
                                        bool isRequest, HttpRequest* req, HttpResponse* rsp);

    static void parseStartLine(const std::string& line, bool isRequest,
                               HttpRequest* req, HttpResponse* rsp);

    static void parseHeaderLine(const std::string& line,
                                std::unordered_map<std::string, std::string>& headers);

    static std::string toLower(const std::string& s);
};
