/**
 * @file    SceneInternMsgRegister.h
 * @brief  场景服区内 S2S 消息注册
 */

#pragma once

class SceneServer;

/** @brief 注册场景服区内消息（含 GW_CLIENT_MSG 解包转发） */
void SceneInternMsgRegister(SceneServer& server);
