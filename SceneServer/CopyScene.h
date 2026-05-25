/**
 * @file    CopyScene.h
 * @brief  副本场景 —— 继承 Scene，按副本类型扩展
 */

#pragma once
#include "Scene.h"
#include <cstdint>
#include <memory>

/** @brief 创建副本时的参数 */
struct CopySceneDef
{
    uint64_t    copyInstanceId = INVALID_SCENE_INSTANCE_ID;
    CopyType    copyType       = CopyType::TEAM;
    uint32_t    mapId          = 0;
    uint64_t    ownerId        = 0;
    uint32_t    maxPlayer      = 5;
    std::string mapName;
    std::string mapFile;
};

/**
 * @brief 副本场景基类
 */
class CopyScene : public Scene
{
public:
    SceneKind getSceneKind() const override { return SceneKind::COPY; }

    CopyType getCopyType() const { return copyType; }
    uint64_t getOwnerId() const { return ownerId; }

protected:
    CopyScene(uint32_t sceneServerId, const CopySceneDef& def);

    bool onLoadResources() override;

    CopyType copyType = CopyType::TEAM;
    uint64_t ownerId  = 0;
};

/** @brief 组队副本 */
class TeamCopyScene : public CopyScene
{
public:
    explicit TeamCopyScene(uint32_t sceneServerId, const CopySceneDef& def);

protected:
    void onStartedHook() override;
};

/** @brief 单人副本 */
class SoloCopyScene : public CopyScene
{
public:
    explicit SoloCopyScene(uint32_t sceneServerId, const CopySceneDef& def);
};

/** @brief 公会副本 */
class GuildCopyScene : public CopyScene
{
public:
    explicit GuildCopyScene(uint32_t sceneServerId, const CopySceneDef& def);
};

/**
 * @brief 副本工厂 —— 按 copyType 创建不同副本子类
 */
class CopySceneFactory
{
public:
    static std::shared_ptr<CopyScene> create(uint32_t sceneServerId,
                                             const CopySceneDef& def);
};
