/**
 * @file    TaskManager.cpp
 * @brief  TaskManager 任务增删与脏标记
 */

#include "TaskManager.h"

bool TaskManager::init()
{
    taskMap.clear();
    dirty = false;
    return true;
}

void TaskManager::loop(uint64_t /*nowMs*/)
{
}

bool TaskManager::needSave() const
{
    return dirty;
}

bool TaskManager::save()
{
    dirty = false;
    return true;
}

bool TaskManager::load()
{
    return true;
}

bool TaskManager::add(uint32_t taskId, uint32_t initProgress)
{
    if (taskId == 0) return false;
    taskMap[taskId] = TaskState{0, initProgress};
    dirty = true;
    return true;
}

bool TaskManager::remove(uint32_t taskId, uint32_t /*unused*/)
{
    if (taskMap.erase(taskId) == 0) return false;
    dirty = true;
    return true;
}
