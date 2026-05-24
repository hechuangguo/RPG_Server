/**
 * @file    AOIServer/main.cpp
 * @brief   AOI（兴趣区域）服务器启动入口
 *
 * AOIServer 负责管理游戏场景中实体的兴趣区域（Area of Interest），
 * 处理实体进入/离开视野、广播范围内的实体信息等。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 ../config/config.xml）
 *   3. 初始化日志（默认 logs/aoi.log）
 *   4. 创建 AOIServer 实例，绑定 0.0.0.0:aoiPort 并连接 SuperServer 与 SessionServer
 *   5. 进入 Run() 主循环
 *
 * 监听端口: aoiPort（配置文件指定）
 * 连接上游: SuperServer（superIP:superPort）+ SessionServer（127.0.0.1:sessionPort）
 */

#include "AOIServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <csignal>

/**
 * @brief AOI服务器启动入口
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
    Logger::Instance().SetPath(cfg.logPaths.count("AOIServer")
                                ? cfg.logPaths.at("AOIServer") : "logs/aoi.log");
    AOIServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.aoiPort,
                     cfg.superIP, (uint16_t)cfg.superPort,
                     "127.0.0.1", (uint16_t)cfg.sessionPort)) return 1;
    server.Run();
    return 0;
}
