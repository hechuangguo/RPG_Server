/**
 * @file    MsgDispatcher.h
 * @brief  消息分发器（module+sub → Handler 哈希映射）
 */

#pragma once
#include "../net/MsgId.h"
#include "Singleton.h"
#include <cstdint>
#include <unordered_map>
#include <functional>
#include <utility>

using MsgHandler = std::function<void(uint32_t connID, const char* data, uint16_t len)>;

/**
 * @brief 消息分发器（单例）
 */
class MsgDispatcher : public LazySingleton<MsgDispatcher>
{
public:
    friend class LazySingleton<MsgDispatcher>;
    /** @brief 获取全局唯一实例（与既有调用方式兼容） */
    static MsgDispatcher& Instance()
    {
        return LazySingleton<MsgDispatcher>::Instance();
    }

    /**
     * @brief 注册消息处理函数（module/sub 形式）
     * @param module 协议模块号
     * @param sub    协议子号
     * @param handler 收到对应消息时执行的处理回调
     */
    void Register(uint8_t module, uint8_t sub, MsgHandler handler)
    {
        m_handlers[makeMsgKey(module, sub)] = std::move(handler);
    }

    /**
     * @brief 注册消息处理函数（扁平协议号形式）
     * @param flatMsgId 扁平协议号（高字节 module，低字节 sub）
     * @param handler   收到对应消息时执行的处理回调
     */
    void Register(uint16_t flatMsgId, MsgHandler handler)
    {
        Register(msgModule(flatMsgId), msgSub(flatMsgId), std::move(handler));
    }

    /**
     * @brief 分发消息到已注册处理器（module/sub 形式）
     * @param connID 连接 ID
     * @param module 协议模块号
     * @param sub    协议子号
     * @param data   消息体指针，可为 nullptr（len=0 时）
     * @param len    消息体长度，单位字节
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

    /**
     * @brief 分发消息到已注册处理器（扁平协议号形式）
     * @param connID    连接 ID
     * @param flatMsgId 扁平协议号（高字节 module，低字节 sub）
     * @param data      消息体指针，可为 nullptr（len=0 时）
     * @param len       消息体长度，单位字节
     * @return 找到并执行处理器返回 true；未注册返回 false
     */
    bool Dispatch(uint32_t connID, uint16_t flatMsgId,
                  const char* data, uint16_t len)
    {
        return Dispatch(connID, msgModule(flatMsgId), msgSub(flatMsgId), data, len);
    }

private:
    /** @brief 私有构造函数（单例） */
    MsgDispatcher() = default;
    /** @brief 消息处理表：key=makeMsgKey(module,sub)，value=处理回调 */
    std::unordered_map<uint32_t, MsgHandler> m_handlers;
};
