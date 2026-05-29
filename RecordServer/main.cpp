/**
 * @file    RecordServer/main.cpp
 * @brief   记录服务器（数据库/存档）启动入口
 *
 * RecordServer 负责玩家数据持久化，处理存档读写请求，是游戏数据的存储中枢。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 config/config.xml）
 *   3. 初始化日志（默认 logs/record.log）
 *   4. 创建 RecordServer 实例，绑定 0.0.0.0:recordPort 开始监听
 *   5. 进入 Run() 主循环
 *
 * 监听端口: recordPort（配置文件指定）
 */

#include "RecordServer.h"
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
        ServerBootstrap::logPathFor(cfg, "RecordServer", "logs/record.log"));

    auto* server = RecordServer::Instance();
    if (!server->Init("0.0.0.0", (uint16_t)cfg.recordPort, cfg)) return 1;
    server->Run();
    return 0;
}
