/**
 * @file    RecordServer/main.cpp
 * @brief   记录服务器（数据库/存档）启动入口
 *
 * RecordServer 负责玩家数据持久化，处理存档读写请求，是游戏数据的存储中枢。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 ../config/config.xml）
 *   3. 初始化日志（默认 logs/record.log）
 *   4. 创建 RecordServer 实例，绑定 0.0.0.0:recordPort 开始监听
 *   5. 进入 Run() 主循环
 *
 * 监听端口: recordPort（配置文件指定）
 */

#include "RecordServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <csignal>

/**
 * @brief 记录服务器启动入口
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
    Logger::Instance().SetPath(cfg.logPaths.count("RecordServer")
                                ? cfg.logPaths.at("RecordServer") : "logs/record.log");
    RecordServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.recordPort, cfg)) return 1;
    server.Run();
    return 0;
}
