/**
 * @file    LoginGameZoneAuthMsg.h
 * @brief   LoginServer 票据校验与最近角色回填（经 Super 外联转发）
 */

#pragma once

class LoginServer;

/** @brief 注册 LOGIN_VERIFY_TOKEN / LOGIN_UPDATE_LAST_USER 处理器 */
void LoginGameZoneAuthMsgRegister(LoginServer& server);
