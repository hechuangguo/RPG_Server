/**
 * @file    ServerBootstrap.h
 * @brief   各服务器进程启动时的配置加载辅助
 */

#pragma once

#include "ConfigLoader.h"
#include "SceneInfoLoader.h"
#include "XmlConfigUtil.h"

#include <cstdio>
#include <string>

namespace ServerBootstrap {

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

} // namespace ServerBootstrap
