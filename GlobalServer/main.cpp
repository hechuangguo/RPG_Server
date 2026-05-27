/**
 * @file    GlobalServer/main.cpp
 * @brief   全局服务器启动入口
 *
 * GlobalServer 管理跨场景的游戏全局数据，处理全服公告、排行榜、
 * 全局活动等需要跨场景协调的业务逻辑。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 config/config.xml）
 *   3. 初始化日志（默认 logs/global.log）
 *   4. 创建 GlobalServer 实例，绑定 0.0.0.0:globalPort 开始监听
 *   5. 进入 Run() 主循环
 *
 * 监听端口: globalPort（配置文件指定）
 */

#include "GlobalServer.h"
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
        ServerBootstrap::logPathFor(cfg, "GlobalServer", "logs/global.log"));

    GlobalServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.globalPort)) return 1;
    server.Run();
    return 0;
}
