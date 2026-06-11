/**
 * @file    SceneServer/main.cpp
 * @brief   场景服务器启动入口
 *
 * SceneServer 负责管理游戏场景实例，加载场景配置并驱动场景中所有实体的逻辑更新。
 * 场景信息由 SceneInfoLoader 从独立配置文件加载。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载服务配置（默认 config/config.xml）
 *   3. 通过 SceneInfoLoader 加载场景信息（默认 config/server_info.xml）
 *   4. 初始化日志（默认 logs/scene.log）
 *   5. 通过单例获取 SceneServer 实例，绑定 0.0.0.0:scenePort 开始监听
 *   6. 进入 Run() 主循环
 *
 * 监听端口: scenePort（配置文件指定）
 * 命令行参数: argv[1]=config.xml, argv[2]=server_info.xml
 */

#include "SceneServer.h"
#include "../sdk/util/ServerBootstrap.h"
#include <csignal>

/**
 * @brief 场景服务器启动入口
 * @return 0 正常退出，1 初始化失败
 */
int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);

    ServerConfig cfg;
    const char* cfgPath = nullptr;
    if (!ServerBootstrap::loadGlobalConfig(argc, argv, cfg, cfgPath))
        return 1;

    SceneServerInfo sceneInfo;
    const char* sceneInfoPath = nullptr;
    if (!ServerBootstrap::loadSceneInfo(argc, argv, sceneInfo, sceneInfoPath))
        return 1;

    Logger::Instance().SetPath(
        ServerBootstrap::logPathFor(cfg, "SceneServer", "logs/scene.log"));

    uint32_t selfId = sceneInfo.sceneID ? sceneInfo.sceneID
                                        : ServerBootstrap::resolveServerID();
    ServerList list;
    if (!ServerBootstrap::fetchServerList(cfg, SubServerType::SCENE, selfId, list))
        return 1;
    const ServerEntry* self = list.find(SubServerType::SCENE, selfId);
    if (!self)
        self = list.findFirst(SubServerType::SCENE);
    if (!self)
    {
        std::fprintf(stderr, "ServerList missing SCENE entry\n");
        return 1;
    }

    LoginServerList loginList;
    ServerBootstrap::loadLoginServerList(argc, argv, loginList);

    auto* server = SceneServer::Instance();
    if (!server->Init("0.0.0.0", self->port, cfg, sceneInfo, list, selfId)) return 1;
    server->setupExternalClients(loginList);
    server->Run();
    return 0;
}
