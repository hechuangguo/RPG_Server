/**
 * @file    LoginGameZoneZoneMsg.h
 * @brief   LoginServer 游戏区状态上报（Super → RegisterListen）
 */

#pragma once

class LoginServer;

/** @brief 注册 LOGIN_ZONE_STATUS_REPORT handler */
void LoginGameZoneZoneMsgRegister(LoginServer& server);
