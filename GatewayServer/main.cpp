/**
 * @file    GatewayServer/main.cpp
 * @brief   网关服务器启动入口
 *
 * GatewayServer 是客户端与服务器集群之间的通信桥梁，负责客户端连接管理、
 * 协议转发、消息路由等。同时监听客户端端口和内部服务端口。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 config/config.xml）
 *   3. 初始化日志（默认 logs/gateway.log）
 *   4. 创建 GatewayServer 实例，分别绑定客户端端口和内部端口
 *   5. 进入 Run() 主循环
 *
 * 监听端口:
 *   - clientPort（外网）: gatewayPort（配置文件指定）
 *   - innerPort（内网）: gatewayPort + 10000
 */

#include "GatewayServer.h"
#include "../sdk/util/ServerBootstrap.h"
#include <csignal>

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerBootstrap::applyDaemonFlag(argc, argv);

    ServerConfig cfg;
    const char* cfgPath = nullptr;
    if (!ServerBootstrap::loadGlobalConfig(argc, argv, cfg, cfgPath))
        return 1;

    Logger::Instance().SetPath(
        ServerBootstrap::logPathFor(cfg, "GatewayServer", "logs/gateway.log"));

    uint32_t selfId = ServerBootstrap::resolveServerID();
    ServerList list;
    if (!ServerBootstrap::fetchServerList(cfg, SubServerType::GATEWAY, selfId, list))
        return 1;
    const ServerEntry* self = list.find(SubServerType::GATEWAY, selfId);
    if (!self)
    {
        std::fprintf(stderr, "ServerList missing GATEWAY entry id=%u\n", selfId);
        return 1;
    }

    auto* server = GatewayServer::Instance();
    uint16_t clientPort = self->port;
    uint16_t innerPort  = (uint16_t)(self->port + 10000);
    LoginServerList loginList;
    ServerBootstrap::loadLoginServerList(argc, argv, loginList);

    if (!server->Init(clientPort, innerPort, cfg, list, selfId)) return 1;
    server->setupExternalClients(loginList);
    server->Run();
    return 0;
}
