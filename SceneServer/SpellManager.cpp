/**
 * @file    SpellManager.cpp
 * @brief  SpellManager 技能增删与脏标记
 */

#include "SpellManager.h"

bool SpellManager::init()
{
    spellLevels.clear();
    dirty = false;
    return true;
}

void SpellManager::loop(uint64_t /*nowMs*/)
{
}

bool SpellManager::needSave() const
{
    return dirty;
}

bool SpellManager::save()
{
    dirty = false;
    return true;
}

bool SpellManager::load()
{
    return true;
}

bool SpellManager::add(uint32_t spellId, uint32_t level)
{
    if (spellId == 0 || level == 0) return false;
    spellLevels[spellId] = level;
    dirty = true;
    return true;
}

bool SpellManager::remove(uint32_t spellId, uint32_t /*unused*/)
{
    if (spellLevels.erase(spellId) == 0) return false;
    dirty = true;
    return true;
}
