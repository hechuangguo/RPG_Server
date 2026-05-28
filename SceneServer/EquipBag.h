/**
 * @file    EquipBag.h
 * @brief  装备栏包裹（继承 Bag）
 *
 * 固定 EQUIP_BAG_MAX_SLOT 个槽位；由 BagManager::bagList 注册为 unique_ptr 实例。
 */

#pragma once
#include "Bag.h"

/** @brief 装备栏槽位数量 */
constexpr uint16_t EQUIP_BAG_MAX_SLOT = 16;

/**
 * @brief 装备包裹
 */
class EquipBag : public Bag
{
public:
    /** @brief 构造装备包并按固定槽位初始化 */
    EquipBag();
    BagType bagType() const override { return BagType::EQUIP; }
};
