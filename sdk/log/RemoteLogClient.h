/**
 * @file    RemoteLogClient.h
 * @brief   远程日志转发 —— 游戏区进程向 LoggerServer 发送 LOG_WRITE_REQ
 *
 * 本地 Logger 落盘行为不变；连接 LoggerServer 成功时额外转发日志行。
 */

#pragma once

#include "../../protocal/InternalMsg.h"
#include "../net/TcpClient.h"

#include <cstddef>

/**
 * @brief 远程日志客户端（进程内单例式静态绑定）
 */
class RemoteLogClient
{
public:
    /**
     * @brief 绑定出站 TcpClient 与来源服务器类型
     * @param client     到 LoggerServer 的连接；nullptr 表示关闭远程转发
     * @param serverType 本进程 SubServerType（写入 Msg_Log_WriteReq）
     */
    static void bind(TcpClient* client, SubServerType serverType);

    /**
     * @brief 尝试转发一条已格式化的日志行
     * @param lv      日志级别
     * @param line    完整日志行（含换行）
     * @param lineLen 字节长度
     */
    static void trySend(int level, const char* line, size_t lineLen);

private:
    static TcpClient*     s_client;
    static SubServerType  s_serverType;
};
