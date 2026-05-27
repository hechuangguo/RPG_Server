/**
 * @file    BagManager.cpp
 * @brief  BagManager 包裹列表与多包裹操作
 */

#include "BagManager.h"
#include "EquipBag.h"
#include "StoreBag.h"

namespace
{
BagType toBagType(uint32_t bagType)
{
    return bagType == static_cast<uint32_t>(BagType::STORE) ? BagType::STORE : BagType::EQUIP;
}
}

void BagManager::registerDefaultBags()
{
    bagList.emplace_back(std::make_unique<EquipBag>());
    bagList.emplace_back(std::make_unique<StoreBag>());
}

Bag* BagManager::findBagByType(BagType bagType)
{
    for (const auto& bag : bagList)
    {
        if (bag && bag->bagType() == bagType)
            return bag.get();
    }
    return nullptr;
}

const Bag* BagManager::findBagByType(BagType bagType) const
{
    for (const auto& bag : bagList)
    {
        if (bag && bag->bagType() == bagType)
            return bag.get();
    }
    return nullptr;
}

bool BagManager::init()
{
    dirty = false;
    bagList.clear();
    registerDefaultBags();

    for (const auto& bag : bagList)
    {
        if (!bag || !bag->init())
            return false;
    }
    return true;
}

void BagManager::loop(uint64_t /*nowMs*/)
{
}

bool BagManager::needSave() const
{
    return dirty;
}

bool BagManager::save()
{
    dirty = false;
    return true;
}

bool BagManager::load()
{
    return init();
}

bool BagManager::add(uint32_t bagType, uint32_t itemId, uint32_t slot)
{
    return addItem(toBagType(bagType), static_cast<uint16_t>(slot), itemId, 1);
}

bool BagManager::remove(uint32_t bagType, uint32_t itemId, uint32_t slot)
{
    (void)itemId;
    return removeItem(toBagType(bagType), static_cast<uint16_t>(slot), 1);
}

Bag* BagManager::getBagByType(BagType bagType)
{
    return findBagByType(bagType);
}

const Bag* BagManager::getBagByType(BagType bagType) const
{
    return findBagByType(bagType);
}

void BagManager::forEachBag(const std::function<void(Bag&)>& fn)
{
    for (const auto& bag : bagList)
    {
        if (bag)
            fn(*bag);
    }
}

bool BagManager::addItem(BagType bagType, uint16_t slot, uint32_t itemId, uint32_t count)
{
    Bag* bag = getBagByType(bagType);
    if (!bag || !bag->isValidSlot(slot) || itemId == 0 || count == 0) return false;

    BagSlotItem* exist = bag->getItemBySlot(slot);
    if (exist && exist->itemId != itemId) return false;

    if (exist)
        exist->count += count;
    else
        bag->setItemBySlot(slot, itemId, count);

    dirty = true;
    return true;
}

bool BagManager::removeItem(BagType bagType, uint16_t slot, uint32_t count)
{
    Bag* bag = getBagByType(bagType);
    if (!bag || !bag->isValidSlot(slot) || count == 0) return false;

    BagSlotItem* item = bag->getItemBySlot(slot);
    if (!item || item->count < count) return false;

    item->count -= count;
    if (item->count == 0)
        bag->clearItemBySlot(slot);
    dirty = true;
    return true;
}

bool BagManager::mergeItem(BagType bagType, uint16_t srcSlot, uint16_t dstSlot)
{
    if (srcSlot == dstSlot) return false;
    Bag* bag = getBagByType(bagType);
    if (!bag) return false;

    BagSlotItem* src = bag->getItemBySlot(srcSlot);
    BagSlotItem* dst = bag->getItemBySlot(dstSlot);
    if (!src || !dst || src->itemId != dst->itemId) return false;

    dst->count += src->count;
    bag->clearItemBySlot(srcSlot);
    dirty = true;
    return true;
}

bool BagManager::splitItem(BagType bagType, uint16_t srcSlot, uint16_t dstSlot, uint32_t splitCount)
{
    if (srcSlot == dstSlot || splitCount == 0) return false;
    Bag* bag = getBagByType(bagType);
    if (!bag || !bag->isValidSlot(srcSlot) || !bag->isValidSlot(dstSlot)) return false;

    BagSlotItem* src = bag->getItemBySlot(srcSlot);
    if (!src || src->count <= splitCount || bag->getItemBySlot(dstSlot) != nullptr) return false;

    src->count -= splitCount;
    bag->setItemBySlot(dstSlot, src->itemId, splitCount);
    dirty = true;
    return true;
}
