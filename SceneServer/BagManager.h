/**
 * @file    BagManager.h
 * @brief  包裹管理器 —— 以列表统一管理所有 Bag 实例
 *
 * 每个 SceneUser 持有独立 BagManager；bagList 以 unique_ptr 持有 EquipBag、StoreBag 等子类，
 * 对外通过 Bag* 访问。init 时注册默认包裹，后续可扩展 registerDefaultBags 增加新类型。
 * 本阶段 save/load 仅内存占位，不落 Record。
 */

#pragma once
#include "Bag.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

/**
 * @brief 用户包裹集合管理器
 */
class BagManager
{
public:
    BagManager() = default;
    ~BagManager() = default;

    BagManager(const BagManager&) = delete;
    BagManager& operator=(const BagManager&) = delete;

    /** @brief 清空并注册默认包裹，再逐个 init */
    bool init();

    /** @brief 每帧驱动（预留 CD、限时道具等） */
    void loop(uint64_t nowMs);

    /** @brief 是否有未落盘变更 */
    bool needSave() const;

    /** @brief 生成内存快照并清 dirty */
    bool save();

    /** @brief 从内存快照恢复（当前等价于 init） */
    bool load();

    /**
     * @brief 通用添加（转发 addItem，数量固定为 1）
     * @param bagType 见 BagType 数值
     * @param itemId  物品模板 ID
     * @param slot    目标槽位
     */
    bool add(uint32_t bagType, uint32_t itemId, uint32_t slot);

    /**
     * @brief 通用删除（转发 removeItem，数量固定为 1）
     * @param itemId 保留参数，当前未校验
     */
    bool remove(uint32_t bagType, uint32_t itemId, uint32_t slot);

    /** @brief 按类型在 bagList 中查找包裹，返回裸指针 */
    Bag* getBagByType(BagType bagType);
    const Bag* getBagByType(BagType bagType) const;

    /** @brief 遍历 bagList 中所有包裹 */
    void forEachBag(const std::function<void(Bag&)>& fn);

    /**
     * @brief 向指定包裹槽位添加/堆叠道具
     * @note 同槽已有不同 itemId 时失败
     */
    bool addItem(BagType bagType, uint16_t slot, uint32_t itemId, uint32_t count);

    /** @brief 从槽位扣除数量，扣光则清空槽 */
    bool removeItem(BagType bagType, uint16_t slot, uint32_t count);

    /** @brief 将源槽全部数量合并到目标槽（须同 itemId） */
    bool mergeItem(BagType bagType, uint16_t srcSlot, uint16_t dstSlot);

    /**
     * @brief 从源槽拆分 splitCount 到空的目标槽
     * @note 目标槽必须为空
     */
    bool splitItem(BagType bagType, uint16_t srcSlot, uint16_t dstSlot, uint32_t splitCount);

private:
    /** @brief 向 bagList 注册 EquipBag、StoreBag 等默认实例 */
    void registerDefaultBags();

    Bag*       findBagByType(BagType bagType);
    const Bag* findBagByType(BagType bagType) const;

    std::vector<std::unique_ptr<Bag>> bagList; /**< 包裹实例列表（唯一所有权） */
    bool                              dirty = false;
};
