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

    uint32_t selfId = ServerBootstrap::resolveServerID();
    ServerList list;
    if (!ServerBootstrap::fetchServerList(cfg, SubServerType::RECORD, selfId, list))
        return 1;
    const ServerEntry* self = list.find(SubServerType::RECORD, selfId);
    if (!self)
    {
        std::fprintf(stderr, "ServerList missing RECORD entry id=%u\n", selfId);
        return 1;
    }

    LoginServerList loginList;
    ServerBootstrap::loadLoginServerList(argc, argv, loginList);

    auto* server = RecordServer::Instance();
    if (!server->Init("0.0.0.0", self->port, cfg, list, selfId)) return 1;
    server->setupExternalClients(loginList);
    server->Run();
    return 0;
}
