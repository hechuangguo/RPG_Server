/**
 * @file    SuperZoneStatusMsg.h
 * @brief   SuperServer 游戏区状态周期上报 LoginServer
 */

#pragma once

class SuperServer;

/** @brief 注册区状态上报定时器（在 SuperServer::Init 之后调用） */
void SuperZoneStatusMsgRegister(SuperServer& super);
