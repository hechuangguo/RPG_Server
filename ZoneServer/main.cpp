/**
 * @file    ZoneServer/main.cpp
 * @brief   区域服务器（分区/分线）启动入口
 *
 * ZoneServer 管理游戏世界中的逻辑分区，支持多线负载均衡。
 * 每个 Zone 实例可承载独立的玩家群组。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 ../config/config.xml）
 *   3. 初始化日志（默认 logs/zone.log）
 *   4. 创建 ZoneServer 实例，绑定 0.0.0.0:zonePort 开始监听
 *   5. 进入 Run() 主循环
 *
 * 监听端口: zonePort（配置文件指定）
 */

#include "ZoneServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <csignal>

/**
 * @brief 区域服务器启动入口
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
    Logger::Instance().SetPath(cfg.logPaths.count("ZoneServer")
                                ? cfg.logPaths.at("ZoneServer") : "logs/zone.log");
    ZoneServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.zonePort)) return 1;
    server.Run();
    return 0;
}
