/**
 * @file    SceneLoginMsg.h
 * @brief   SceneServer 处理经 Super 转发的 Login 区服指令
 *
 * 注册：
 *   - SS_EXTERN_FWD_RSP（0x1F11）：Login/外联回包到 Scene（按 innerMsgId/seq 占位）
 *   - LOGIN_GM_CMD_REQ（0x1905）：Login 经 Super 下发的 GM 骨架（Scene 侧重执行）
 */

#pragma once

class SceneServer;

/**
 * @brief 注册 SceneServer Login 相关区内 handler
 * @param server SceneServer 实例
 */
void SceneLoginMsgRegister(SceneServer& server);
