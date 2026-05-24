/**
 * @file    SessionServer/main.cpp
 * @brief   会话服务器启动入口
 *
 * SessionServer 管理玩家会话生命周期，负责登录验证、连接保活与会话状态维护。
 * 启动后向 SuperServer 注册自身，以便其他服务发现并连接。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 ../config/config.xml）
 *   3. 初始化日志（默认 logs/session.log）
 *   4. 创建 SessionServer 实例，绑定 0.0.0.0:sessionPort 并连接 SuperServer
 *   5. 进入 Run() 主循环
 *
 * 监听端口: sessionPort
 * 连接上游: SuperServer（superIP:superPort）
 */

#include "SessionServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <csignal>

/**
 * @brief 会话服务器启动入口
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
    Logger::Instance().SetPath(cfg.logPaths.count("SessionServer")
                                ? cfg.logPaths.at("SessionServer") : "logs/session.log");
    SessionServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.sessionPort,
                     cfg.superIP, (uint16_t)cfg.superPort)) return 1;
    server.Run();
    return 0;
}
