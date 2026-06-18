/**
 * @file    SessionInternMsgRegister.h
 * @brief  会话服区内 S2S 消息注册
 */

#pragma once

class SessionServer;

/** @brief 注册会话服区内消息（含 GW_CLIENT_MSG 解包转发） */
void SessionInternMsgRegister(SessionServer& server);
