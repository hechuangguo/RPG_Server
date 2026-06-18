/**
 * @file    SceneClientMsgRegister.h
 * @brief  场景服经网关转发的客户端消息注册
 */

#pragma once

class SceneServer;

/** @brief 注册场景服处理的客户端 C2S 消息 */
void SceneClientMsgRegister(SceneServer& server);
