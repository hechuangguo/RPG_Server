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
    uint64_t    copyInstanceId = INVALID_SCENE_INSTANCE_ID; /**< 副本实例 ID */
    CopyType    copyType       = CopyType::TEAM;            /**< 副本类型 */
    uint32_t    mapId          = 0;                         /**< 地图模板 ID */
    uint64_t    ownerId        = 0;                         /**< 副本归属者 ID */
    uint32_t    maxPlayer      = 5;                         /**< 最大人数 */
    std::string mapName;                                    /**< 地图名 */
    std::string mapFile;                                    /**< 地图资源路径 */
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
    /** @brief 用副本定义构造基础场景信息 */
    CopyScene(uint32_t sceneServerId, const CopySceneDef& def);

    /** @brief 副本资源加载钩子 */
    bool onLoadResources() override;

    CopyType copyType = CopyType::TEAM; /**< 副本类型 */
    uint64_t ownerId  = 0;              /**< 副本归属者 */
};

/** @brief 组队副本 */
class TeamCopyScene : public CopyScene
{
public:
    /** @brief 构造组队副本 */
    explicit TeamCopyScene(uint32_t sceneServerId, const CopySceneDef& def);

protected:
    void onStartedHook() override;
};

/** @brief 单人副本 */
class SoloCopyScene : public CopyScene
{
public:
    /** @brief 构造单人副本 */
    explicit SoloCopyScene(uint32_t sceneServerId, const CopySceneDef& def);
};

/** @brief 公会副本 */
class GuildCopyScene : public CopyScene
{
public:
    /** @brief 构造公会副本 */
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
