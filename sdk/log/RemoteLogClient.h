/**
 * @file    RemoteLogClient.h
 * @brief   远程日志转发 —— 游戏区进程经 Super 向 LoggerServer 发送 LOG_WRITE_REQ
 */

#pragma once

#include "../../protocal/InternalMsg.h"

#include <cstddef>

class GameZoneExternSender;

/**
 * @brief 远程日志客户端（进程内单例式静态绑定）
 */
class RemoteLogClient
{
public:
    /** @brief 绑定经 Super 转发的 GameZoneExternSender */
    static void bind(GameZoneExternSender* sender, SubServerType serverType);

    /** @brief 尝试转发一条已格式化的日志行 */
    static void trySend(int level, const char* line, size_t lineLen);

private:
    static GameZoneExternSender* s_sender;
    static SubServerType         s_serverType;
};
