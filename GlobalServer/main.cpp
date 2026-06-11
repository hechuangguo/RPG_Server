/**
 * @file    GlobalServer/main.cpp
 * @brief   全区数据服务器（外联可选）启动入口
 *
 * 读取 GlobalServer/extern_global.xml；游戏区经 loginserverlist.xml 连接本进程。
 */

#include "GlobalServer.h"
#include "../sdk/util/ServerBootstrap.h"
#include "../sdk/util/ExternServerConfig.h"
#include <csignal>
#include <cstdio>

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);

    ExternServerConfig extCfg;
    const char* extPath = ServerBootstrap::externConfigPath(
        argc, argv, 1, XmlConfig::ENV_EXTERN_GLOBAL_CONFIG,
        XmlConfig::EXTERN_GLOBAL_CONFIG_DEFAULT);
    std::string err;
    if (!ExternServerConfigLoader::Load(extPath, extCfg, &err))
    {
        std::fprintf(stderr, "Failed to load %s\n  %s\n", extPath, err.c_str());
        return 1;
    }
    if (extCfg.listenPort == 0)
    {
        std::fprintf(stderr, "extern_global.xml: Listen port is 0\n");
        return 1;
    }

    const char* logPath = extCfg.logPath.empty() ? "logs/global.log" : extCfg.logPath.c_str();
    Logger::Instance().SetPath(logPath);

    auto* server = GlobalServer::Instance();
    if (!server->Init(extCfg))
        return 1;
    server->Run();
    return 0;
}
