/**
 * @file    SceneEntry.cpp
 * @brief  SceneEntry 基础实现
 */

#include "SceneEntry.h"

SceneEntry::SceneEntry(EntryID id)
    : entryId(id)
{
}

void SceneEntry::loop(uint64_t /*nowMs*/)
{
}
