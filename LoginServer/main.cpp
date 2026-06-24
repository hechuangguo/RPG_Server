/**
 * @file    LoginServer/main.cpp
 * @brief   外联登录服务器启动入口
 *
 * 读取 LoginServer/extern_login.xml；双端口监听（客户端 + 网关注册）。
 */

#include "LoginServer.h"
#include "../sdk/net/NetTls.h"
#include "../sdk/util/ProductionConfigValidator.h"
#include "../sdk/util/ServerBootstrap.h"

#include <csignal>
#include <cstdio>

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerBootstrap::applyDaemonFlag(argc, argv);

    LoginExternConfig cfg;
    const char* extPath = ServerBootstrap::externConfigPath(
        argc, argv, 1, XmlConfig::ENV_EXTERN_LOGIN_CONFIG,
        XmlConfig::EXTERN_LOGIN_CONFIG_DEFAULT);
    std::string err;
    if (!LoginExternConfigLoader::Load(extPath, cfg, &err))
    {
        std::fprintf(stderr, "Failed to load %s\n  %s\n", extPath, err.c_str());
        return 1;
    }

    if (!initNetTls(cfg.tls))
        return 1;

    const bool enforceProduction = std::getenv("RPG_PRODUCTION") != nullptr;
    const ProductionConfigCheckResult prodCheck =
        validateProductionConfig(cfg.tls, cfg.database.pass, enforceProduction);
    if (!prodCheck.ok)
    {
        std::fprintf(stderr, "生产配置校验失败: %s\n", prodCheck.message.c_str());
        return 1;
    }

    Logger::Instance().SetPath(cfg.logPath.empty() ? "logs/login.log" : cfg.logPath);

    auto* server = LoginServer::Instance();
    if (!server->Init(cfg))
        return 1;
    server->Run();
    return 0;
}
