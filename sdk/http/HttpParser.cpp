/**
 * @file    HttpParser.cpp
 * @brief   HTTP/1.1 增量解析实现
 */

#include "HttpParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace
{
size_t findHeaderEnd(const char* data, size_t len)
{
    if (len < 4)
        return std::string::npos;
    for (size_t i = 0; i + 3 < len; ++i)
    {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n')
            return i;
    }
    return std::string::npos;
}

void splitLines(const char* headerBlock, size_t headerLen, std::vector<std::string>& lines)
{
    size_t pos = 0;
    while (pos < headerLen)
    {
        size_t end = pos;
        while (end + 1 < headerLen && !(headerBlock[end] == '\r' && headerBlock[end + 1] == '\n'))
            ++end;
        if (end > pos)
            lines.emplace_back(headerBlock + pos, end - pos);
        pos = end + 2;
    }
}

int parseContentLength(const std::unordered_map<std::string, std::string>& headers)
{
    auto it = headers.find("content-length");
    if (it == headers.end())
        return 0;
    return std::atoi(it->second.c_str());
}
}  // namespace

std::string HttpParser::toLower(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

void HttpParser::parseStartLine(const std::string& line, bool isRequest,
                                HttpRequest* req, HttpResponse* rsp)
{
    size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos)
        return;
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos)
        return;

    if (isRequest && req)
    {
        req->method  = line.substr(0, sp1);
        req->path    = line.substr(sp1 + 1, sp2 - sp1 - 1);
        req->version = line.substr(sp2 + 1);
    }
    else if (!isRequest && rsp)
    {
        rsp->version = line.substr(0, sp1);
        rsp->status  = std::atoi(line.substr(sp1 + 1, sp2 - sp1 - 1).c_str());
        rsp->reason  = line.substr(sp2 + 1);
    }
}

void HttpParser::parseHeaderLine(const std::string& line,
                                 std::unordered_map<std::string, std::string>& headers)
{
    size_t colon = line.find(':');
    if (colon == std::string::npos)
        return;
    std::string key = toLower(line.substr(0, colon));
    std::string val = line.substr(colon + 1);
    while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front())))
        val.erase(val.begin());
    headers[key] = val;
}

HttpParseResult HttpParser::parseMessage(const char* data, size_t len, size_t& consumed,
                                         bool isRequest, HttpRequest* req, HttpResponse* rsp)
{
    consumed = 0;
    size_t headerEnd = findHeaderEnd(data, len);
    if (headerEnd == std::string::npos)
        return HttpParseResult::NEED_MORE;

    const size_t headerTotal = headerEnd + 4;
    std::vector<std::string> lines;
    splitLines(data, headerEnd, lines);
    if (lines.empty())
        return HttpParseResult::ERROR;

    std::unordered_map<std::string, std::string> headers;
    parseStartLine(lines[0], isRequest, req, rsp);
    for (size_t i = 1; i < lines.size(); ++i)
        parseHeaderLine(lines[i], headers);

    const int contentLen = parseContentLength(headers);
    if (len < headerTotal + static_cast<size_t>(contentLen))
        return HttpParseResult::NEED_MORE;

    std::string body;
    if (contentLen > 0)
        body.assign(data + headerTotal, static_cast<size_t>(contentLen));

    consumed = headerTotal + static_cast<size_t>(contentLen);
    if (isRequest && req)
        req->headers = std::move(headers);
    else if (!isRequest && rsp)
    {
        rsp->headers = std::move(headers);
        rsp->body    = std::move(body);
    }
    if (isRequest && req)
        req->body = body;
    return HttpParseResult::OK;
}

HttpParseResult HttpParser::parseRequest(const char* data, size_t len, size_t& consumed,
                                         HttpRequest& out)
{
    out = HttpRequest{};
    return parseMessage(data, len, consumed, true, &out, nullptr);
}

HttpParseResult HttpParser::parseResponse(const char* data, size_t len, size_t& consumed,
                                          HttpResponse& out)
{
    out = HttpResponse{};
    return parseMessage(data, len, consumed, false, nullptr, &out);
}
