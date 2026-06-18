/**
 * @file    LoginClientMsgRegister.h
 * @brief  登录服客户端口消息注册
 */

#pragma once

class LoginServer;

/** @brief 注册登录服客户端口 C2S 消息处理器 */
void LoginClientMsgRegister(LoginServer& server);
