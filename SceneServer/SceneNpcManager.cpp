/**
 * @file    SceneNpcManager.cpp
 * @brief  SceneNpcManager 实现
 */

#include "SceneNpcManager.h"

std::shared_ptr<SceneNpc> SceneNpcManager::findNpc(EntryID npcId) const
{
    auto it = m_npcs.find(npcId);
    return it != m_npcs.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<SceneNpc>> SceneNpcManager::findNpcsByMap(uint32_t mapId) const
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

std::shared_ptr<SceneNpc> SceneNpcManager::createNpc(const SceneNpcDef& def)
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

bool SceneNpcManager::addNpc(EntryID npcId, std::shared_ptr<SceneNpc> npc)
{
    if (!npc || npcId == INVALID_ENTRY_ID) return false;
    m_npcs[npcId] = std::move(npc);
    return true;
}

bool SceneNpcManager::removeNpc(EntryID npcId)
{
    return m_npcs.erase(npcId) > 0;
}

size_t SceneNpcManager::getNpcCount() const
{
    return m_npcs.size();
}

size_t SceneNpcManager::getNpcCountByMap(uint32_t mapId) const
{
    size_t n = 0;
    for (const auto& [id, npc] : m_npcs)
    {
        (void)id;
        if (npc && npc->getMapId() == mapId) ++n;
    }
    return n;
}

void SceneNpcManager::loopAll(uint64_t nowMs)
{
    for (auto& [id, npc] : m_npcs)
    {
        (void)id;
        if (npc) npc->loop(nowMs);
    }
}

void SceneNpcManager::forEach(
    const std::function<void(EntryID, const std::shared_ptr<SceneNpc>&)>& fn) const
{
    for (const auto& [npcId, npc] : m_npcs)
        fn(npcId, npc);
}
