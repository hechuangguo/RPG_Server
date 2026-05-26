/**
 * @file    ClientMsgRouter.h
 * @brief  Gateway 客户端消息路由目标（本地 / Scene / Session）
 */

#pragma once
#include "../common/ClientMsg.h"
#include <cstdint>

/** @brief 转发目标 */
enum class ClientForwardTarget : uint8_t
{
    LOCAL  = 0,
    SCENE  = 1,
    SESSION = 2,
    DROP   = 3,
};

/**
 * @brief 按 module/sub 决定转发目标
 */
class ClientMsgRouter
{
public:
    static ClientForwardTarget resolve(uint8_t module, uint8_t sub)
    {
        (void)sub;
        switch (static_cast<ClientModule>(module))
        {
        case ClientModule::LOGIN:
        case ClientModule::SYSTEM:
            return ClientForwardTarget::LOCAL;
        case ClientModule::SOCIAL:
        case ClientModule::QUEST:
            return ClientForwardTarget::SESSION;
        case ClientModule::SCENE:
        case ClientModule::BATTLE:
        case ClientModule::BAG:
        case ClientModule::SKILL:
        case ClientModule::NPC:
            return ClientForwardTarget::SCENE;
        case ClientModule::CHAT:
            /* 地图广播走 Scene；私聊 0x03 走 Session */
            if (sub == 0x03)
                return ClientForwardTarget::SESSION;
            return ClientForwardTarget::SCENE;
        default:
            return ClientForwardTarget::DROP;
        }
    }
};
