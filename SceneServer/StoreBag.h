/**
 * @file    StoreBag.h
 * @brief  仓库包裹（继承 Bag）
 *
 * 固定 STORE_BAG_MAX_SLOT 个槽位；由 BagManager::bagList 注册，便于扩展更多 Bag 子类。
 */

#pragma once
#include "Bag.h"

/** @brief 仓库槽位数量 */
constexpr uint16_t STORE_BAG_MAX_SLOT = 120;

/**
 * @brief 仓库包裹
 */
class StoreBag : public Bag
{
public:
    StoreBag();
    BagType bagType() const override { return BagType::STORE; }
};
