/**
 * @file    Bag.cpp
 * @brief  Bag 槽位校验、遍历与读写实现
 */

#include "Bag.h"

Bag::Bag(uint16_t maxSlot)
    : m_maxSlot(maxSlot > 0 ? maxSlot : 1)
{
}

bool Bag::init()
{
    m_slotItems.clear();
    return true;
}

bool Bag::isValidSlot(uint16_t slot) const
{
    return slot < m_maxSlot;
}

const BagSlotItem* Bag::getItemBySlot(uint16_t slot) const
{
    if (!isValidSlot(slot)) return nullptr;
    auto it = m_slotItems.find(slot);
    return it != m_slotItems.end() ? &it->second : nullptr;
}

BagSlotItem* Bag::getItemBySlot(uint16_t slot)
{
    if (!isValidSlot(slot)) return nullptr;
    auto it = m_slotItems.find(slot);
    return it != m_slotItems.end() ? &it->second : nullptr;
}

void Bag::forEachSlot(const std::function<void(uint16_t, const BagSlotItem&)>& fn) const
{
    for (const auto& [slot, item] : m_slotItems)
        fn(slot, item);
}

bool Bag::setItemBySlot(uint16_t slot, uint32_t itemId, uint32_t count)
{
    if (!isValidSlot(slot) || itemId == 0 || count == 0) return false;
    m_slotItems[slot] = BagSlotItem{itemId, count};
    return true;
}

bool Bag::clearItemBySlot(uint16_t slot)
{
    if (!isValidSlot(slot)) return false;
    m_slotItems.erase(slot);
    return true;
}
