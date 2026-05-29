/**
 * @file    LoggerServer/main.cpp
 * @brief   日志服务器启动入口
 *
 * LoggerServer 负责接收、汇聚各服务器节点的日志输出，统一写入本地文件系统。
 * 提供集中式日志管理，方便调试和运维。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 config/config.xml）
 *   3. 初始化自身日志（默认 logs/logger.log），并从路径中提取日志目录
 *   4. 创建 LoggerServer 实例，绑定 0.0.0.0:loggerPort 并连接 SuperServer 与 SessionServer
 *   5. 进入 Run() 主循环
 *
 * 监听端口: loggerPort（配置文件指定）
 * 连接上游: SuperServer（superIP:superPort）+ SessionServer（127.0.0.1:sessionPort）
 * 日志目录: 自动从 logger.log 路径提取
 */

#include "LoggerServer.h"
#include "../sdk/util/ServerBootstrap.h"
#include <csignal>

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);

    ServerConfig cfg;
    const char* cfgPath = nullptr;
    if (!ServerBootstrap::loadGlobalConfig(argc, argv, cfg, cfgPath))
        return 1;

    std::string loggerPath =
        ServerBootstrap::logPathFor(cfg, "LoggerServer", "logs/logger.log");
    Logger::Instance().SetPath(loggerPath);

    std::string logDir = "logs";
    const size_t slash = loggerPath.rfind('/');
    if (slash != std::string::npos)
        logDir = loggerPath.substr(0, slash);

    auto* server = LoggerServer::Instance();
    if (!server->Init("0.0.0.0", (uint16_t)cfg.loggerPort,
                      cfg.superIP, (uint16_t)cfg.superPort,
                      "127.0.0.1", (uint16_t)cfg.sessionPort,
                      logDir)) return 1;
    server->Run();
    return 0;
}
