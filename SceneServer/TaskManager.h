/**
 * @file    TaskManager.h
 * @brief  任务管理器（questId → 状态与进度）
 *
 * status 语义与 tables/init.sql 中 t_quest.status 一致：0 进行中 / 1 完成 / 2 已领奖。
 */

#pragma once
#include <cstdint>
#include <unordered_map>

/** @brief 单任务运行时状态 */
struct TaskState
{
    uint32_t status   = 0;  /**< 0=进行中 1=已完成 2=已领奖 */
    uint32_t progress = 0;  /**< 进度值（击杀数、收集数等） */
};

/**
 * @brief 任务进度管理器
 */
class TaskManager
{
public:
    bool init();
    void loop(uint64_t nowMs);
    bool needSave() const;
    bool save();
    bool load();

    /**
     * @brief 接取任务
     * @param taskId        任务模板 ID
     * @param initProgress  初始进度，默认 0
     */
    bool add(uint32_t taskId, uint32_t initProgress = 0);

    /** @brief 移除任务记录 */
    bool remove(uint32_t taskId, uint32_t unused = 0);

private:
    std::unordered_map<uint32_t, TaskState> taskMap;
    bool dirty = false;
};
