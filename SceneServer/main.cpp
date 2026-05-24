/**
 * @file    SceneServer/main.cpp
 * @brief   场景服务器启动入口
 *
 * SceneServer 负责管理游戏场景实例，加载场景配置并驱动场景中所有实体的逻辑更新。
 * 场景信息由 SceneInfoLoader 从独立配置文件加载。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载服务配置（默认 ../config/config.xml）
 *   3. 通过 SceneInfoLoader 加载场景信息（默认 ../config/server_info.xml）
 *   4. 初始化日志（默认 logs/scene.log）
 *   5. 创建 SceneServer 实例，绑定 0.0.0.0:scenePort 开始监听
 *   6. 进入 Run() 主循环
 *
 * 监听端口: scenePort（配置文件指定）
 * 命令行参数: argv[1] = 配置文件路径, argv[2] = 场景信息文件路径
 */

#include "SceneServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <csignal>
#include "../sdk/util/SceneInfoLoader.h"

/**
 * @brief 场景服务器启动入口
 * @param argc 命令行参数个数
 * @param argv[1] 可选：配置文件路径（默认 ../config/config.xml）
 * @param argv[2] 可选：场景信息文件路径（默认 ../config/server_info.xml）
 * @return 0 正常退出，1 初始化失败
 */
int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerConfig cfg;
    const char* cfgPath       = (argc > 1) ? argv[1] : "../config/config.xml";
    const char* sceneInfoPath = (argc > 2) ? argv[2] : "../config/server_info.xml";
    ConfigLoader::Load(cfgPath, cfg);
    SceneServerInfo sceneInfo;
    SceneInfoLoader::Load(sceneInfoPath, sceneInfo);
    Logger::Instance().SetPath(cfg.logPaths.count("SceneServer")
                                ? cfg.logPaths.at("SceneServer") : "logs/scene.log");
    SceneServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.scenePort, cfg, sceneInfo)) return 1;
    server.Run();
    return 0;
}
