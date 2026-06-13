/**
 * @file    SceneLoginMsg.h
 * @brief  SceneServer 处理经 Super 转发的 Login 区服指令
 */

#pragma once

class SceneServer;

/** @brief 注册 SS_EXTERN_FWD_RSP 及 Login 下发骨架处理器 */
void SceneLoginMsgRegister(SceneServer& server);
