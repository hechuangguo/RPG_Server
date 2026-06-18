/**
 * @file    ClientMsgDispatcher.h
 * @brief  客户端 wire 消息分发器（与区内 MsgDispatcher 物理隔离）
 */

#pragma once

#include "../net/MsgId.h"
#include "Singleton.h"
#include <cstdint>
#include <functional>
#include <unordered_map>

/** @brief 客户端消息处理回调（connID + 包体） */
using ClientMsgHandler = std::function<void(uint32_t connID, const char* data, uint16_t len)>;

/**
 * @brief 客户端 module/sub 分发器（单例）
 *
 * 与 MsgDispatcher（区内 InternalMsgID）分离，避免协议键冲突。
 */
class ClientMsgDispatcher : public LazySingleton<ClientMsgDispatcher>
{
public:
    friend class LazySingleton<ClientMsgDispatcher>;

    /** @brief 获取全局唯一实例 */
    static ClientMsgDispatcher& Instance()
    {
        return LazySingleton<ClientMsgDispatcher>::Instance();
    }

    /**
     * @brief 注册客户端消息处理函数
     * @param module  客户端协议模块号
     * @param sub     客户端协议子号
     * @param handler 处理回调
     */
    void Register(uint8_t module, uint8_t sub, ClientMsgHandler handler)
    {
        m_handlers[makeMsgKey(module, sub)] = std::move(handler);
    }

    /**
     * @brief 注册客户端消息处理函数（扁平 module|sub）
     * @param flatMsgId 高字节 module，低字节 sub
     */
    void Register(uint16_t flatMsgId, ClientMsgHandler handler)
    {
        Register(msgModule(flatMsgId), msgSub(flatMsgId), std::move(handler));
    }

    /**
     * @brief 分发客户端消息
     * @return 找到并执行处理器返回 true；未注册返回 false
     */
    bool Dispatch(uint32_t connID, uint8_t module, uint8_t sub,
                  const char* data, uint16_t len)
    {
        auto it = m_handlers.find(makeMsgKey(module, sub));
        if (it == m_handlers.end())
            return false;
        it->second(connID, data, len);
        return true;
    }

    /** @brief 分发客户端消息（扁平协议号） */
    bool Dispatch(uint32_t connID, uint16_t flatMsgId,
                  const char* data, uint16_t len)
    {
        return Dispatch(connID, msgModule(flatMsgId), msgSub(flatMsgId), data, len);
    }

private:
    ClientMsgDispatcher() = default;

    std::unordered_map<uint32_t, ClientMsgHandler> m_handlers;
};
