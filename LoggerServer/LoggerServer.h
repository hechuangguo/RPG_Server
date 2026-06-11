/**
 * @file    LoggerServer.h
 * @brief  日志服务器 —— 接收各服务器日志写入请求，统一落盘
 *
 * ## 职责
 * - 接收来自游戏区的 LOG_WRITE_REQ 消息
 * - 按服务器类型双文件落盘：scene.log + scene.log.YYYYMMDD-HH
 *
 * ## 部署
 * - 可选外联服，独立部署在任意机器；监听配置见 LoggerServer/extern_logger.xml
 * - 游戏区通过 loginserverlist.xml 连接本服务，不向 SuperServer 注册
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/Singleton.h"
#include "../sdk/log/Logger.h"
#include "../sdk/log/LogFileWriter.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <string>

/**
 * @brief LoggerServer 核心类
 *
 * 单进程运行，维护按服务器类型索引的 LogFileWriter 句柄表。
 */
class LoggerServer : public INetCallback, public LazySingleton<LoggerServer>
{
public:
    friend class LazySingleton<LoggerServer>;
    /** @brief 获取 LoggerServer 单例指针 */
    static LoggerServer* Instance() { return &LazySingleton<LoggerServer>::Instance(); }

private:
    /** @brief 构造 LoggerServer（初始化写入器索引） */
    LoggerServer();

public:
    /**
     * @brief 初始化 LoggerServer（仅监听，无区内出站连接）
     * @param ip     监听 IP
     * @param port   监听端口（来自 extern_logger.xml）
     * @param logDir 日志输出根目录
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port, const std::string& logDir);

    /** @brief 主循环 */
    void Run();

    void OnConnect(ConnID id) override;
    void OnDisconnect(ConnID id) override;
    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    void RegisterHandlers();
    void OnWriteLog(ConnID fromConn, const char* data, uint16_t len);
    static const char* ServerLogBaseName(SubServerType type);
    LogFileWriter& GetWriter(SubServerType type);

    TcpServer  m_server;   /**< 接收游戏区远程日志的连接 */
    std::string m_logDir;  /**< 日志输出根目录 */
    std::unordered_map<int, LogFileWriter> m_writers; /**< 按 SubServerType 索引的写入器 */
};
