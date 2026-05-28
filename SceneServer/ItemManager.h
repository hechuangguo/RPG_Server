/**
 * @file    ItemManager.h
 * @brief  道具管理器（模板 ID → 数量）
 *
 * 与 BagManager 格子数据可并存：此处维护简化道具计数，便于任务/掉落校验。
 * 每个 SceneUser 独立实例；save/load 当前为内存占位。
 */

#pragma once
#include <cstdint>
#include <unordered_map>

/**
 * @brief 用户道具计数管理器
 */
class ItemManager
{
public:
    /** @brief 初始化道具计数容器 */
    bool init();

    /** @brief 每帧驱动（预留时效道具） */
    void loop(uint64_t nowMs);

    /** @brief 是否存在待保存改动 */
    bool needSave() const;

    /** @brief 保存并清理脏标记 */
    bool save();

    /** @brief 加载道具计数快照 */
    bool load();

    /**
     * @brief 增加道具数量
     * @param itemId 物品模板 ID
     * @param count  增加数量，默认 1
     */
    bool add(uint32_t itemId, uint32_t count = 1);

    /**
     * @brief 扣除道具数量
     * @return 数量不足时 false
     */
    bool remove(uint32_t itemId, uint32_t count = 1);

private:
    std::unordered_map<uint32_t, uint32_t> itemMap; /**< templateId → count */
    bool dirty = false;                              /**< 脏标记 */
};
