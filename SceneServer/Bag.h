/**
 * @file    Bag.h
 * @brief  包裹基类与格子数据定义
 *
 * 提供槽位校验、按槽读写、遍历格子等基础能力；具体容量由子类 EquipBag/StoreBag 决定。
 * 不直接对接 RecordServer，由 BagManager 聚合后统一 needSave/save/load。
 */

#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>

/** @brief 包裹类型（与 BagManager::getBagByType 一致） */
enum class BagType : uint8_t
{
    EQUIP = 1,  /**< 装备栏 */
    STORE = 2,  /**< 仓库 */
};

/** @brief 单个包裹格子内的道具快照 */
struct BagSlotItem
{
    uint32_t itemId = 0;  /**< 物品模板 ID，0 表示空格 */
    uint32_t count  = 0;  /**< 堆叠数量 */
};

/**
 * @brief 包裹基类
 *
 * 槽位数据存于 m_slotItems（稀疏 map）；有效槽位范围为 [0, m_maxSlot)。
 */
class Bag
{
public:
    /**
     * @brief 构造指定最大槽位数的包裹
     * @param maxSlot 槽位上限（不含），由子类传入 EQUIP_BAG_MAX_SLOT 等
     */
    explicit Bag(uint16_t maxSlot);
    virtual ~Bag() = default;

    /** @brief 子类包裹类型标识 */
    virtual BagType bagType() const = 0;

    /** @brief 清空所有格子 */
    bool init();

    /** @brief 最大槽位数（不含） */
    uint16_t getMaxSlot() const { return m_maxSlot; }

    /** @brief 槽位是否在合法范围内 */
    bool isValidSlot(uint16_t slot) const;

    /**
     * @brief 只读获取槽位道具
     * @return 空格或非法槽返回 nullptr
     */
    const BagSlotItem* getItemBySlot(uint16_t slot) const;

    /**
     * @brief 可写获取槽位道具
     * @return 空格或非法槽返回 nullptr
     */
    BagSlotItem* getItemBySlot(uint16_t slot);

    /**
     * @brief 遍历已有道具的槽位（仅非空格）
     * @param fn 回调 (slot, item)
     */
    void forEachSlot(const std::function<void(uint16_t, const BagSlotItem&)>& fn) const;

    /**
     * @brief 设置槽位道具（覆盖）
     * @return 非法槽或 itemId/count 为 0 时 false
     */
    bool setItemBySlot(uint16_t slot, uint32_t itemId, uint32_t count);

    /** @brief 清空指定槽位 */
    bool clearItemBySlot(uint16_t slot);

protected:
    uint16_t m_maxSlot = 0;                              /**< 槽位上限 */
    std::unordered_map<uint16_t, BagSlotItem> m_slotItems; /**< slot → 道具 */
};
