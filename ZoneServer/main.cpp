/**
 * @file    ZoneServer/main.cpp
 * @brief   跨区转发服务器（外联可选）启动入口
 *
 * 读取 ZoneServer/extern_zone.xml；游戏区经 loginserverlist.xml 连接本进程。
 */

#include "ZoneServer.h"
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
        argc, argv, 1, XmlConfig::ENV_EXTERN_ZONE_CONFIG,
        XmlConfig::EXTERN_ZONE_CONFIG_DEFAULT);
    std::string err;
    if (!ExternServerConfigLoader::Load(extPath, extCfg, &err))
    {
        std::fprintf(stderr, "Failed to load %s\n  %s\n", extPath, err.c_str());
        return 1;
    }
    if (extCfg.listenPort == 0)
    {
        std::fprintf(stderr, "extern_zone.xml: Listen port is 0\n");
        return 1;
    }

    const char* logPath = extCfg.logPath.empty() ? "logs/zone.log" : extCfg.logPath.c_str();
    Logger::Instance().SetPath(logPath);

    auto* server = ZoneServer::Instance();
    if (!server->Init(extCfg))
        return 1;
    server->Run();
    return 0;
}
