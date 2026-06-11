/**
 * @file    SuperServer/main.cpp
 * @brief   超级服务器（管理中心）启动入口
 *
 * SuperServer 是整个服务器集群的管理节点，负责进程注册、状态监控与调度。
 * 其他服务器启动后首先向 SuperServer 注册，由 SuperServer 统一管理拓扑关系。
 *
 * 启动流程：
 *   1. 注册 SIGINT/SIGTERM 信号处理，支持优雅关闭
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 config/config.xml）
 *   3. 初始化日志（默认 logs/super.log）
 *   4. 创建 SuperServer 实例，绑定 superIP:superPort 开始监听
 *   5. 进入 Run() 主循环直至收到退出信号
 *
 * 监听端口: superPort（配置文件指定，其他服务器主动连接此端口注册）
 */

#include "SuperServer.h"
#include "../sdk/util/ServerBootstrap.h"
#include <csignal>

static bool g_running = true;
void SignalHandler(int) { g_running = false; }

/**
 * @brief 超级服务器启动入口
 * @param argv[1] 可选：配置文件路径（默认 config/config.xml，或环境变量 RPG_CONFIG_PATH）
 * @return 0 正常退出，1 初始化失败
 */
int main(int argc, char* argv[])
{
    signal(SIGINT,  SignalHandler);
    signal(SIGTERM, SignalHandler);
    signal(SIGPIPE, SIG_IGN);
    ServerBootstrap::applyDaemonFlag(argc, argv);

    ServerConfig cfg;
    const char* cfgPath = nullptr;
    if (!ServerBootstrap::loadGlobalConfig(argc, argv, cfg, cfgPath))
        return 1;

    Logger::Instance().SetPath(
        ServerBootstrap::logPathFor(cfg, "SuperServer", "logs/super.log"));

    LoginServerList loginList;
    ServerBootstrap::loadLoginServerList(argc, argv, loginList);

    auto* server = SuperServer::Instance();
    if (!server->Init(cfg.superIP, (uint16_t)cfg.superPort, cfg)) return 1;
    server->setupExternalClients(loginList);
    server->Run();
    return 0;
}
