/**
 * @file    GatewayServer/main.cpp
 * @brief   网关服务器启动入口
 *
 * GatewayServer 是客户端与服务器集群之间的通信桥梁，负责客户端连接管理、
 * 协议转发、消息路由等。同时监听客户端端口和内部服务端口。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 ../config/config.xml）
 *   3. 初始化日志（默认 logs/gateway.log）
 *   4. 创建 GatewayServer 实例，分别绑定客户端端口和内部端口
 *   5. 进入 Run() 主循环
 *
 * 监听端口:
 *   - clientPort（外网）: gatewayPort（配置文件指定）
 *   - innerPort（内网）: gatewayPort + 10000
 */

#include "GatewayServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <csignal>

/**
 * @brief 网关服务器启动入口
 * @param argc 命令行参数个数
 * @param argv[1] 可选：配置文件路径（默认 ../config/config.xml）
 * @return 0 正常退出，1 初始化失败
 */
int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerConfig cfg;
    const char* cfgPath = (argc > 1) ? argv[1] : "../config/config.xml";
    ConfigLoader::Load(cfgPath, cfg);
    Logger::Instance().SetPath(cfg.logPaths.count("GatewayServer")
                                ? cfg.logPaths.at("GatewayServer") : "logs/gateway.log");
    GatewayServer server;
    uint16_t clientPort = (uint16_t)cfg.gatewayPort;
    uint16_t innerPort  = (uint16_t)(cfg.gatewayPort + 10000);
    if (!server.Init(clientPort, innerPort, cfg)) return 1;
    server.Run();
    return 0;
}
