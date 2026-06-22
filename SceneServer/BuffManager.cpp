/**
 * @file    BuffManager.cpp
 * @brief  BuffManager 过期清理与增删
 */

#include "BuffManager.h"

bool BuffManager::init()
{
    buffMap.clear();
    dirty = false;
    return true;
}

void BuffManager::loop(uint64_t nowMs)
{
    for (auto it = buffMap.begin(); it != buffMap.end();)
    {
        if (it->second.expireAtMs > 0 && it->second.expireAtMs <= nowMs)
        {
            it = buffMap.erase(it);
            dirty = true;
        }
        else
        {
            ++it;
        }
    }
}

bool BuffManager::needSave() const
{
    return dirty;
}

bool BuffManager::save()
{
    dirty = false;
    return true;
}

bool BuffManager::load()
{
    return true;
}

bool BuffManager::add(uint32_t buffId, uint32_t durationMs, uint64_t nowMs)
{
    if (buffId == 0) return false;
    BuffState state{};
    if (durationMs > 0)
        state.expireAtMs = nowMs + static_cast<uint64_t>(durationMs);
    else
        state.expireAtMs = 0;
    buffMap[buffId] = state;
    dirty = true;
    return true;
}

bool BuffManager::remove(uint32_t buffId, uint32_t /*unused*/)
{
    if (buffMap.erase(buffId) == 0) return false;
    dirty = true;
    return true;
}
