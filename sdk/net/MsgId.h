/**
 * @file    MsgId.h
 * @brief  消息 module/sub 与扁平协议号互转
 */

#pragma once
#include <cstdint>

/** @brief 由模块号与子号合成扁平 ID（高字节 module，低字节 sub） */
inline uint16_t makeMsgId(uint8_t module, uint8_t sub)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(module) << 8) | sub);
}

/** @brief 从扁平 ID 取模块号 */
inline uint8_t msgModule(uint16_t flatMsgId)
{
    return static_cast<uint8_t>(flatMsgId >> 8);
}

/** @brief 从扁平 ID 取子号 */
inline uint8_t msgSub(uint16_t flatMsgId)
{
    return static_cast<uint8_t>(flatMsgId & 0xFF);
}

/** @brief 合成 MsgDispatcher 查表键 */
inline uint32_t makeMsgKey(uint8_t module, uint8_t sub)
{
    return (static_cast<uint32_t>(module) << 8) | sub;
}

inline uint32_t makeMsgKey(uint16_t flatMsgId)
{
    return static_cast<uint32_t>(flatMsgId);
}
