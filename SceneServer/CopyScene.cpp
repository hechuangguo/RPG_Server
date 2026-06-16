/**
 * @file    CopyScene.cpp
 * @brief  CopyScene 及工厂实现
 */

#include "CopyScene.h"
#include "../sdk/log/Logger.h"

CopyScene::CopyScene(uint32_t sceneServerId, const CopySceneDef& def)
    : Scene(sceneServerId, def.copyInstanceId,
            MapConfig{def.mapId, def.mapName, def.mapFile, def.maxPlayer})
    , copyType(def.copyType)
    , ownerId(def.ownerId)
{
}

bool CopyScene::onLoadResources()
{
    LOG_INFO("副本场景加载: instance=%llu type=%u owner=%llu map=%u",
             sceneInstanceId, static_cast<uint32_t>(copyType), ownerId, mapId);
    return Scene::onLoadResources();
}

TeamCopyScene::TeamCopyScene(uint32_t sceneServerId, const CopySceneDef& def)
    : CopyScene(sceneServerId, def)
{
}

void TeamCopyScene::onStartedHook()
{
    LOG_DEBUG("组队副本启动完成: instance=%llu", sceneInstanceId);
}

SoloCopyScene::SoloCopyScene(uint32_t sceneServerId, const CopySceneDef& def)
    : CopyScene(sceneServerId, def)
{
}

GuildCopyScene::GuildCopyScene(uint32_t sceneServerId, const CopySceneDef& def)
    : CopyScene(sceneServerId, def)
{
}

std::shared_ptr<CopyScene> CopySceneFactory::create(uint32_t sceneServerId,
                                                      const CopySceneDef& def)
{
    switch (def.copyType)
    {
    case CopyType::SOLO:
        return std::make_shared<SoloCopyScene>(sceneServerId, def);
    case CopyType::GUILD:
        return std::make_shared<GuildCopyScene>(sceneServerId, def);
    case CopyType::TEAM:
    default:
        return std::make_shared<TeamCopyScene>(sceneServerId, def);
    }
}
