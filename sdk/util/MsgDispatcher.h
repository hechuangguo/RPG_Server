/**
 * @file    MsgDispatcher.h
 * @brief  消息分发器（module+sub → Handler 哈希映射）
 */

#pragma once
#include "../net/MsgId.h"
#include <cstdint>
#include <unordered_map>
#include <functional>
#include <utility>

using MsgHandler = std::function<void(uint32_t connID, const char* data, uint16_t len)>;

/**
 * @brief 消息分发器（单例）
 */
class MsgDispatcher
{
public:
    static MsgDispatcher& Instance()
    {
        static MsgDispatcher s;
        return s;
    }

    void Register(uint8_t module, uint8_t sub, MsgHandler handler)
    {
        m_handlers[makeMsgKey(module, sub)] = std::move(handler);
    }

    void Register(uint16_t flatMsgId, MsgHandler handler)
    {
        Register(msgModule(flatMsgId), msgSub(flatMsgId), std::move(handler));
    }

    bool Dispatch(uint32_t connID, uint8_t module, uint8_t sub,
                  const char* data, uint16_t len)
    {
        auto it = m_handlers.find(makeMsgKey(module, sub));
        if (it == m_handlers.end())
            return false;
        it->second(connID, data, len);
        return true;
    }

    bool Dispatch(uint32_t connID, uint16_t flatMsgId,
                  const char* data, uint16_t len)
    {
        return Dispatch(connID, msgModule(flatMsgId), msgSub(flatMsgId), data, len);
    }

private:
    std::unordered_map<uint32_t, MsgHandler> m_handlers;
};
