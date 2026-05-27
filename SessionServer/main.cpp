/**
 * @file    SessionServer/main.cpp
 * @brief   会话服务器启动入口
 *
 * SessionServer 管理玩家会话生命周期，负责登录验证、连接保活与会话状态维护。
 * 启动后向 SuperServer 注册自身，以便其他服务发现并连接。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 config/config.xml）
 *   3. 初始化日志（默认 logs/session.log）
 *   4. 创建 SessionServer 实例，绑定 0.0.0.0:sessionPort 并连接 SuperServer
 *   5. 进入 Run() 主循环
 *
 * 监听端口: sessionPort
 * 连接上游: SuperServer（superIP:superPort）
 */

#include "SessionServer.h"
#include "../sdk/util/ServerBootstrap.h"
#include <csignal>

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);

    ServerConfig cfg;
    const char* cfgPath = nullptr;
    if (!ServerBootstrap::loadGlobalConfig(argc, argv, cfg, cfgPath))
        return 1;

    Logger::Instance().SetPath(
        ServerBootstrap::logPathFor(cfg, "SessionServer", "logs/session.log"));

    SessionServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.sessionPort, cfg)) return 1;
    server.Run();
    return 0;
}
