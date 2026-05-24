/**
 * @file    SceneNpcManager.h
 * @brief  地图 NPC 集合管理（创建、查找、移除、统一 loop）
 */

#pragma once
#include "SceneNpc.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * @brief 管理当前 SceneServer 进程内所有 NPC
 */
class SceneNpcManager
{
public:
    std::shared_ptr<SceneNpc> findNpc(EntryID npcId) const
    {
        auto it = m_npcs.find(npcId);
        return it != m_npcs.end() ? it->second : nullptr;
    }

    /** @brief 获取指定地图上的 NPC 列表 */
    std::vector<std::shared_ptr<SceneNpc>> findNpcsByMap(uint32_t mapId) const
    {
        std::vector<std::shared_ptr<SceneNpc>> out;
        for (const auto& [id, npc] : m_npcs)
        {
            (void)id;
            if (npc && npc->getMapId() == mapId)
                out.push_back(npc);
        }
        return out;
    }

    /** @brief 创建并注册 NPC */
    std::shared_ptr<SceneNpc> createNpc(const SceneNpcDef& def)
    {
        if (def.npcId == INVALID_ENTRY_ID)
            return nullptr;
        if (m_npcs.count(def.npcId))
            return m_npcs[def.npcId];

        auto npc = SceneNpc::create(def);
        if (!npc || !npc->init())
            return nullptr;

        m_npcs[def.npcId] = npc;
        return npc;
    }

    bool addNpc(EntryID npcId, std::shared_ptr<SceneNpc> npc)
    {
        if (!npc || npcId == INVALID_ENTRY_ID) return false;
        m_npcs[npcId] = std::move(npc);
        return true;
    }

    bool removeNpc(EntryID npcId)
    {
        return m_npcs.erase(npcId) > 0;
    }

    size_t getNpcCount() const { return m_npcs.size(); }

    size_t getNpcCountByMap(uint32_t mapId) const
    {
        size_t n = 0;
        for (const auto& [id, npc] : m_npcs)
        {
            (void)id;
            if (npc && npc->getMapId() == mapId) ++n;
        }
        return n;
    }

    /** @brief 驱动所有 NPC 的 loop */
    void loopAll(uint64_t nowMs)
    {
        for (auto& [id, npc] : m_npcs)
        {
            (void)id;
            if (npc) npc->loop(nowMs);
        }
    }

    void forEach(const std::function<void(EntryID, const std::shared_ptr<SceneNpc>&)>& fn) const
    {
        for (const auto& [npcId, npc] : m_npcs)
            fn(npcId, npc);
    }

private:
    std::unordered_map<EntryID, std::shared_ptr<SceneNpc>> m_npcs;
};
