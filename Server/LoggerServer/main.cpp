/**
 * @file    LoggerServer/main.cpp
 * @brief   日志服务器（外联可选）启动入口
 *
 * 读取 LoggerServer/extern_logger.xml 绑定监听；游戏区经 loginserverlist.xml 连接本进程。
 */

#include "LoggerServer.h"
#include "../sdk/util/ServerBootstrap.h"
#include "../sdk/util/ExternServerConfig.h"
#include <csignal>
#include <cstdio>

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerBootstrap::applyDaemonFlag(argc, argv);

    ExternServerConfig extCfg;
    const char* extPath = ServerBootstrap::externConfigPath(
        argc, argv, 1, XmlConfig::ENV_EXTERN_LOGGER_CONFIG,
        XmlConfig::EXTERN_LOGGER_CONFIG_DEFAULT);
    std::string err;
    if (!ExternServerConfigLoader::Load(extPath, extCfg, &err))
    {
        std::fprintf(stderr, "Failed to load %s\n  %s\n", extPath, err.c_str());
        return 1;
    }
    if (extCfg.listenPort == 0)
    {
        std::fprintf(stderr, "extern_logger.xml: Listen port is 0\n");
        return 1;
    }

    Logger::Instance().SetPath("logs/logger.log");

    auto* server = LoggerServer::Instance();
    if (!server->Init(extCfg.listenIP, extCfg.listenPort, extCfg.logDir))
        return 1;
    server->Run();
    return 0;
}
