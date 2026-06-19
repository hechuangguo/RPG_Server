/**
 * @file    GatewayServer/main.cpp
 * @brief   网关服务器启动入口
 *
 * GatewayServer 是客户端与服务器集群之间的通信桥梁，负责客户端连接管理、
 * 协议转发、消息路由等。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 config/config.xml）
 *   3. 初始化日志（默认 logs/gateway.log）
 *   4. 创建 GatewayServer 实例，绑定客户端端口并出站连接区内服
 *   5. 进入 Run() 主循环
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

    if (!ServerBootstrap::initNetTlsFromConfig(cfg))
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

    if (!server->Init(clientPort, cfg, list, selfId)) return 1;
    server->Run();
    return 0;
}
