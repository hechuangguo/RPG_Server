/**
 * @file    ServerBootstrap.h
 * @brief   各服务器进程启动时的配置加载辅助
 */

#pragma once

#include "ConfigLoader.h"
#include "ExternServerConfig.h"
#include "ExternalServerHub.h"
#include "LoginServerList.h"
#include "SceneInfoLoader.h"
#include "ServerList.h"
#include "../log/RemoteLogClient.h"
#include "XmlConfigUtil.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace ServerBootstrap {

/** @brief 当前进程的服务器实例编号：环境变量 RPG_SERVER_ID（>0）覆盖，默认 1 */
inline uint32_t resolveServerID()
{
    if (const char* env = std::getenv("RPG_SERVER_ID"))
    {
        long v = std::strtol(env, nullptr, 10);
        if (v > 0)
            return (uint32_t)v;
    }
    return 1;
}

/**
 * @brief 启动期从 SuperServer 拉取集群拓扑 ServerList
 * @param cfg       全局配置（提供 SuperServer 地址）
 * @param selfType  本进程服务器类型
 * @param selfId    本进程实例编号
 * @param out       [out] 拉取到的 ServerList
 * @return 成功返回 true；失败已打印 stderr
 */
inline bool fetchServerList(const ServerConfig& cfg, SubServerType selfType,
                            uint32_t selfId, ServerList& out)
{
    if (!ServerListClient::fetch(cfg.superIP, (uint16_t)cfg.superPort,
                                 selfType, selfId, out))
    {
        std::fprintf(stderr,
                     "Failed to fetch ServerList from SuperServer %s:%d\n",
                     cfg.superIP.c_str(), cfg.superPort);
        return false;
    }
    return true;
}

/**
 * @brief 解析全局配置路径
 * @param argc main 参数个数
 * @param argv main 参数数组
 * @return config.xml 最终路径（优先级：argv[1] > 环境变量 > 默认值）
 */
inline const char* globalConfigPath(int argc, char* argv[])
{
    return XmlConfig::resolvePath(argc, argv, 1, XmlConfig::ENV_CONFIG_PATH,
                                  XmlConfig::CONFIG_PATH_DEFAULT);
}

/**
 * @brief 解析 Scene 配置路径
 * @param argc main 参数个数
 * @param argv main 参数数组
 * @return server_info.xml 最终路径（优先级：argv[2] > 环境变量 > 默认值）
 */
inline const char* sceneInfoPath(int argc, char* argv[])
{
    return XmlConfig::resolvePath(argc, argv, 2, XmlConfig::ENV_SCENE_INFO_PATH,
                                  XmlConfig::SCENE_INFO_PATH_DEFAULT);
}

/**
 * @brief 获取指定服务器的日志路径
 * @param cfg 全局配置对象
 * @param serverKey 日志映射键（如 "SceneServer"）
 * @param fallback 映射缺失时使用的兜底路径
 * @return 最终日志路径
 */
inline std::string logPathFor(const ServerConfig& cfg, const char* serverKey,
                              const char* fallback)
{
    const auto it = cfg.logPaths.find(serverKey);
    if (it != cfg.logPaths.end() && !it->second.empty())
        return it->second;
    return fallback;
}

/**
 * @brief 加载 config.xml；失败时打印 stderr 并返回 false
 */
inline bool loadGlobalConfig(int argc, char* argv[], ServerConfig& cfg,
                             const char*& outPath)
{
    std::string err;
    outPath = globalConfigPath(argc, argv);
    if (ConfigLoader::Load(outPath, cfg, &err))
        return true;
    std::fprintf(stderr, "Failed to load config: %s\n  %s\n", outPath, err.c_str());
    return false;
}

/**
 * @brief 加载 server_info.xml；失败时打印 stderr 并返回 false
 */
inline bool loadSceneInfo(int argc, char* argv[], SceneServerInfo& info,
                          const char*& outPath)
{
    std::string err;
    outPath = sceneInfoPath(argc, argv);
    if (SceneInfoLoader::Load(outPath, info, &err))
        return true;
    std::fprintf(stderr, "Failed to load scene info: %s\n  %s\n", outPath, err.c_str());
    return false;
}

/**
 * @brief 解析 loginserverlist.xml 路径（argv 无专用位时用环境变量/默认根目录文件）
 */
inline const char* loginServerListPath(int /*argc*/, char* /*argv*/[])
{
    if (const char* env = std::getenv(XmlConfig::ENV_LOGIN_SERVER_LIST_PATH))
    {
        if (env[0] != '\0')
            return env;
    }
    return XmlConfig::LOGIN_SERVER_LIST_DEFAULT;
}

/**
 * @brief 加载 loginserverlist.xml（文件缺失视为未配置外联，仅 WARN）
 */
inline bool loadLoginServerList(int argc, char* argv[], LoginServerList& out)
{
    std::string err;
    const char* path = loginServerListPath(argc, argv);
    if (!LoginServerListLoader::Load(path, out, &err))
    {
        std::fprintf(stderr, "Failed to load loginserverlist: %s\n  %s\n",
                     path, err.c_str());
        return false;
    }
    if (out.size() == 0)
        std::fprintf(stderr, "Note: no external servers in %s (optional)\n", path);
    return true;
}

/**
 * @brief 解析外联服独立部署配置路径
 */
inline const char* externConfigPath(int argc, char* argv[], int argIndex,
                                    const char* envName, const char* defaultPath)
{
    return XmlConfig::resolvePath(argc, argv, argIndex, envName, defaultPath);
}

/**
 * @brief 装配游戏区外联连接并绑定远程日志
 */
inline void initGameZoneExtern(ExternalServerHub& hub, const LoginServerList& list,
                               SubServerType selfType, bool wantGlobal, bool wantZone)
{
    hub.configure(list, true, wantGlobal, wantZone);
    hub.connectAll();
    if (TcpClient* loggerClient = hub.client(SubServerType::LOGGER))
        RemoteLogClient::bind(loggerClient, selfType);
}

/** @brief 游戏区主循环内外联 poll + 重连 */
inline void tickGameZoneExtern(ExternalServerHub& hub)
{
    hub.poll();
    hub.tickReconnect();
}

} // namespace ServerBootstrap
