/**
 * @file    ClientMsgRouter.h
 * @brief  Gateway 客户端消息路由目标（本地 / Scene / Session）
 */

#pragma once
#include "../Common/ClientMsg.h"
#include <cstdint>

/** @brief 转发目标 */
enum class ClientForwardTarget : uint8_t
{
    LOCAL   = 0, /**< Gateway 本地处理（登录/系统） */
    SCENE   = 1, /**< 转发至 SceneServer */
    SESSION = 2, /**< 转发至 SessionServer */
    DROP    = 3, /**< 丢弃（未知 module） */
};

/**
 * @brief 按 module/sub 决定转发目标
 */
class ClientMsgRouter
{
public:
    /** @brief 按 module/sub 解析客户端消息应转发至的目标服 */
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
