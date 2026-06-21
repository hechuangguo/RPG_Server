/**
 * @file    ClientMsgRouter.h
 * @brief  Gateway 客户端消息路由目标（本地 / Scene / Session / DROP）
 *
 * **当前实现**（与 docs/PROTOCOL.md §2.1 一致）：
 * | module | 目标 |
 * |--------|------|
 * | LOGIN, SYSTEM | LOCAL |
 * | SCENE, NPC, CHAT（全部 sub） | SCENE |
 * | BATTLE, BAG, SKILL, SOCIAL, QUEST | DROP（Validator 白名单未登记） |
 *
 * **规划**（SOCIAL/QUEST proto + SessionClientMsgRegister 落地后）：
 * SOCIAL/QUEST → SESSION；CHAT sub=C2S_WHISPER_REQ → SESSION。
 */

#pragma once
#include "ClientCommon.pb.h"
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
    /** @brief 按 module/sub 解析转发目标；未实现 module 返回 DROP */
    static ClientForwardTarget resolve(uint8_t module, uint8_t /*sub*/)
    {
        switch (static_cast<rpg::client::ClientModule>(module))
        {
        case rpg::client::LOGIN:
        case rpg::client::SYSTEM:
            return ClientForwardTarget::LOCAL;
        case rpg::client::SCENE:
        case rpg::client::NPC:
        case rpg::client::CHAT:
            return ClientForwardTarget::SCENE;
        default:
            return ClientForwardTarget::DROP;
        }
    }
};
