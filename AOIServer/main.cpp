/**
 * @file    AOIServer/main.cpp
 * @brief   AOI（兴趣区域）服务器启动入口
 *
 * AOIServer 负责管理游戏场景中实体的兴趣区域（Area of Interest），
 * 处理实体进入/离开视野、广播范围内的实体信息等。
 *
 * 启动流程：
 *   1. 忽略 SIGPIPE 信号
 *   2. 通过 ConfigLoader 加载 XML 配置（默认 config/config.xml）
 *   3. 初始化日志（默认 logs/aoi.log）
 *   4. 创建 AOIServer 实例，绑定 0.0.0.0:aoiPort 并连接 SuperServer（注册）
 *   5. 进入 Run() 主循环
 *
 * 监听端口: aoiPort（配置文件指定）
 * 连接上游: SuperServer（superIP:superPort）— 不连接 SessionServer
 */

#include "AOIServer.h"
#include "../sdk/util/ServerBootstrap.h"
#include <csignal>

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerBootstrap::applyDaemonFlag(argc, argv);

    ServerConfig cfg;
    const char* cfgPath = nullptr;
    if (!ServerBootstrap::loadGlobalConfig(argc, argv, cfg, cfgPath))
        return 1;

    Logger::Instance().SetPath(
        ServerBootstrap::logPathFor(cfg, "AOIServer", "logs/aoi.log"));

    uint32_t selfId = ServerBootstrap::resolveServerID();
    ServerList list;
    if (!ServerBootstrap::fetchServerList(cfg, SubServerType::AOI, selfId, list))
        return 1;
    const ServerEntry* self = list.find(SubServerType::AOI, selfId);
    if (!self)
    {
        std::fprintf(stderr, "ServerList missing AOI entry id=%u\n", selfId);
        return 1;
    }

    auto* server = AOIServer::Instance();
    if (!server->Init("0.0.0.0", self->port, cfg, list, selfId)) return 1;
    server->Run();
    return 0;
}
