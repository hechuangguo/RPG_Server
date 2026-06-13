/**
 * @file    ItemManager.cpp
 * @brief  ItemManager 道具增减与脏标记
 */

#include "ItemManager.h"

bool ItemManager::init()
{
    itemMap.clear();
    dirty = false;
    return true;
}

void ItemManager::loop(uint64_t /*nowMs*/)
{
}

bool ItemManager::needSave() const
{
    return dirty;
}

bool ItemManager::save()
{
    dirty = false;
    return true;
}

bool ItemManager::load()
{
    return true;
}

bool ItemManager::add(uint32_t itemId, uint32_t count)
{
    if (itemId == 0 || count == 0) return false;
    itemMap[itemId] += count;
    dirty = true;
    return true;
}

bool ItemManager::remove(uint32_t itemId, uint32_t count)
{
    auto it = itemMap.find(itemId);
    if (it == itemMap.end() || count == 0 || it->second < count) return false;
    it->second -= count;
    if (it->second == 0) itemMap.erase(it);
    dirty = true;
    return true;
}
