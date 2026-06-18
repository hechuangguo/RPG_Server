/**
 * @file    ClientMsgRouter.h
 * @brief  Gateway 客户端消息路由目标（本地 / Scene / Session）
 */

#pragma once
#include "../Common/ClientTypes.h"
#include <cstdint>

/** @brief 转发目标 */
enum class ClientForwardTarget : uint8_t
{
    LOCAL   = 0, /**< Gateway 本地处理（登录/系统） */
    SCENE   = 1, /**< 转发至 SceneServer */
    SESSION = 2, /**< 转发至 SessionServer */
    DROP    = 3, /**< 丢弃（未知或未实现 module） */
};

/**
 * @brief 按 module/sub 决定转发目标
 *
 * 仅已实现且 Validator 白名单内的消息会到达此处；未实现域返回 DROP。
 *
 * **Session 客户端上行**：当前 `resolve` **从不**返回 SESSION；会话服仅服间
 * GW_CLIENT_MSG（SOCIAL/QUEST 等玩法立项后需同步扩展 Router + SessionClientMsgRegister）。
 */
class ClientMsgRouter
{
public:
    /** @brief 按 module/sub 解析客户端消息应转发至的目标服 */
    static ClientForwardTarget resolve(uint8_t module, uint8_t /*sub*/)
    {
        switch (static_cast<ClientModule>(module))
        {
        case ClientModule::LOGIN:
        case ClientModule::SYSTEM:
            return ClientForwardTarget::LOCAL;
        case ClientModule::SCENE:
        case ClientModule::NPC:
        case ClientModule::CHAT:
            return ClientForwardTarget::SCENE;
        default:
            return ClientForwardTarget::DROP;
        }
    }
};
