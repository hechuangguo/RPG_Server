/**
 * @file    SuperServer/main.cpp
 * @brief   超级服务器（管理中心）启动入口
 *
 * SuperServer 是整个服务器集群的管理节点，负责进程注册、状态监控与调度。
 * 其他服务器启动后首先向 SuperServer 注册，由 SuperServer 统一管理拓扑关系。
 *
 * 启动流程：
 *   1. 注册 SIGINT/SIGTERM 信号处理，支持优雅关闭
 *   2. 通过 ConfigLoader 加载 XML 配置文件（默认 ../config/config.xml）
 *   3. 初始化日志（默认 logs/super.log）
 *   4. 创建 SuperServer 实例，绑定 superIP:superPort 开始监听
 *   5. 进入 Run() 主循环直至收到退出信号
 *
 * 监听端口: superPort（配置文件指定，其他服务器主动连接此端口注册）
 */

#include "SuperServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <cstdio>
#include <csignal>

static bool g_running = true;
void SignalHandler(int) { g_running = false; }

/**
 * @brief 超级服务器启动入口
 * @param argc 命令行参数个数
 * @param argv[1] 可选：配置文件路径（默认 ../config/config.xml）
 * @return 0 正常退出，1 初始化失败
 */
int main(int argc, char* argv[])
{
    signal(SIGINT,  SignalHandler);
    signal(SIGTERM, SignalHandler);
    signal(SIGPIPE, SIG_IGN);

    ServerConfig cfg;
    const char* cfgPath = (argc > 1) ? argv[1] : "../config/config.xml";
    if (!ConfigLoader::Load(cfgPath, cfg))
    {
        fprintf(stderr, "Failed to load config: %s\n", cfgPath);
        return 1;
    }

    Logger::Instance().SetPath(cfg.logPaths.count("SuperServer")
                               ? cfg.logPaths.at("SuperServer") : "logs/super.log");

    SuperServer server;
    if (!server.Init(cfg.superIP, (uint16_t)cfg.superPort)) return 1;
    server.Run();
    return 0;
}
