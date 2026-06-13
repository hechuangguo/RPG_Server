/**
 * @file    UserLog.h
 * @brief   带用户上下文的本地日志
 *
 * 输出前缀格式：[SceneUser userId=123 name=foo conn=7] message...
 * tag 区分 SceneUser / SessionUser / GatewayUser；底层仍走进程本地 Logger（非 RemoteLog）。
 */

#pragma once

#include "../util/UserBase.h"

/**
 * @brief IUser 派生类共用的 info/debug/warn/error 前缀日志
 */
class UserLog
{
public:
    /**
     * @brief 输出 INFO 级用户上下文日志
     * @param user 用户实例（提供 userId/name/conn）
     * @param tag  用户类型标签（如 "SceneUser"）
     * @param fmt  printf 格式串
     */
    static void info(const IUser& user, const char* tag, const char* fmt, ...);

    /**
     * @brief 输出 DEBUG 级用户上下文日志
     * @param user 用户实例
     * @param tag  用户类型标签
     * @param fmt  printf 格式串
     */
    static void debug(const IUser& user, const char* tag, const char* fmt, ...);

    /**
     * @brief 输出 WARN 级用户上下文日志
     * @param user 用户实例
     * @param tag  用户类型标签
     * @param fmt  printf 格式串
     */
    static void warn(const IUser& user, const char* tag, const char* fmt, ...);

    /**
     * @brief 输出 ERROR 级用户上下文日志
     * @param user 用户实例
     * @param tag  用户类型标签
     * @param fmt  printf 格式串
     */
    static void error(const IUser& user, const char* tag, const char* fmt, ...);
};
