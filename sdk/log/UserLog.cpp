/**
 * @file    UserLog.cpp
 * @brief  UserLog 实现
 */

#include "UserLog.h"
#include "Logger.h"

#include <cstdio>
#include <cstdarg>

namespace
{
void logWithUser(LogLevel lv, const IUser& user, const char* tag,
                 const char* fmt, va_list ap)
{
    char prefix[256];
    std::snprintf(prefix, sizeof(prefix),
                  "[%s userId=%llu name=%s conn=%u] ",
                  tag,
                  static_cast<unsigned long long>(user.GetID()),
                  user.GetName(),
                  user.Base().connID);

    char body[2048];
    std::vsnprintf(body, sizeof(body), fmt, ap);

    char line[2304];
    std::snprintf(line, sizeof(line), "%s%s", prefix, body);
    Logger::Instance().Log(lv, "%s", line);
}
} // namespace

void UserLog::info(const IUser& user, const char* tag, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logWithUser(LogLevel::INFO, user, tag, fmt, ap);
    va_end(ap);
}

void UserLog::debug(const IUser& user, const char* tag, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logWithUser(LogLevel::DEBUG, user, tag, fmt, ap);
    va_end(ap);
}

void UserLog::warn(const IUser& user, const char* tag, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logWithUser(LogLevel::WARN, user, tag, fmt, ap);
    va_end(ap);
}

void UserLog::error(const IUser& user, const char* tag, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logWithUser(LogLevel::ERR, user, tag, fmt, ap);
    va_end(ap);
}
