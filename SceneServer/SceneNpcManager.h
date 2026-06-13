/**
 * @file    SceneNpcManager.h
 * @brief  地图 NPC 集合管理（创建、查找、移除、统一 loop）
 */

#pragma once
#include "SceneNpc.h"
#include "../sdk/util/Singleton.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * @brief 管理当前 SceneServer 进程内所有 NPC（单例）
 */
class SceneNpcManager : public LazySingleton<SceneNpcManager>
{
public:
    friend class LazySingleton<SceneNpcManager>;

    /** @brief 获取全局唯一实例 */
    static SceneNpcManager& Instance() { return LazySingleton<SceneNpcManager>::Instance(); }

    /** @brief 按 npcId 查找 NPC */
    std::shared_ptr<SceneNpc> findNpc(EntryID npcId) const;

    /** @brief 获取指定地图上的 NPC 列表 */
    std::vector<std::shared_ptr<SceneNpc>> findNpcsByMap(uint32_t mapId) const;

    /** @brief 创建并注册 NPC */
    std::shared_ptr<SceneNpc> createNpc(const SceneNpcDef& def);

    /** @brief 注册已有 NPC 实例 */
    bool addNpc(EntryID npcId, std::shared_ptr<SceneNpc> npc);

    /** @brief 移除 NPC */
    bool removeNpc(EntryID npcId);

    /** @brief 进程内 NPC 总数 */
    size_t getNpcCount() const;

    /** @brief 指定地图上的 NPC 数量 */
    size_t getNpcCountByMap(uint32_t mapId) const;

    /** @brief 驱动所有 NPC 的 loop */
    void loopAll(uint64_t nowMs);

    /** @brief 遍历全部 NPC（只读访问） */
    void forEach(const std::function<void(EntryID, const std::shared_ptr<SceneNpc>&)>& fn) const;

private:
    SceneNpcManager() = default;

    std::unordered_map<EntryID, std::shared_ptr<SceneNpc>> m_npcs; /**< npcId -> NPC 对象 */
};
