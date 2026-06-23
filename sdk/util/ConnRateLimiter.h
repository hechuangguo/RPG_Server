/**
 * @file    ConnRateLimiter.h
 * @brief   按连接/键的滑动窗口速率限制（Gateway/Login 防刷）
 */
#pragma once
#include "../net/NetDefine.h"
#include "../timer/TimerMgr.h"
#include <cstdint>
#include <string>
#include <unordered_map>

class ConnRateLimiter
{
public:
    ConnRateLimiter(uint32_t maxPerWindow, uint64_t windowMs)
        : maxPerWindow(maxPerWindow), windowMs(windowMs) {}

    bool allow(ConnID connId)
    {
        const uint64_t nowMs = TimerMgr::NowMs();
        auto& slot = slots[connId];
        if (slot.windowStartMs == 0 || nowMs - slot.windowStartMs >= windowMs)
        {
            slot.windowStartMs = nowMs;
            slot.count = 1;
            return true;
        }
        if (slot.count >= maxPerWindow)
            return false;
        ++slot.count;
        return true;
    }

    bool allowKey(const std::string& key)
    {
        const uint64_t nowMs = TimerMgr::NowMs();
        auto& slot = keySlots[key];
        if (slot.windowStartMs == 0 || nowMs - slot.windowStartMs >= windowMs)
        {
            slot.windowStartMs = nowMs;
            slot.count = 1;
            return true;
        }
        if (slot.count >= maxPerWindow)
            return false;
        ++slot.count;
        return true;
    }

    void erase(ConnID connId) { slots.erase(connId); }

private:
    struct Slot { uint64_t windowStartMs = 0; uint32_t count = 0; };
    uint32_t maxPerWindow;
    uint64_t windowMs;
    std::unordered_map<ConnID, Slot> slots;
    std::unordered_map<std::string, Slot> keySlots;
};
