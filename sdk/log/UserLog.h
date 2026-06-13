/**
 * @file    UserLog.h
 * @brief  带用户上下文的本地日志
 */

#pragma once

#include "../util/UserBase.h"

/**
 * @brief IUser 派生类共用的 info/debug/warn/error 前缀日志
 */
class UserLog
{
public:
    static void info(const IUser& user, const char* tag, const char* fmt, ...);
    static void debug(const IUser& user, const char* tag, const char* fmt, ...);
    static void warn(const IUser& user, const char* tag, const char* fmt, ...);
    static void error(const IUser& user, const char* tag, const char* fmt, ...);
};
