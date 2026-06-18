/**
 * @file    GlobalInternMsgRegister.h
 * @brief   全局服区内 S2S 消息注册聚合
 */

#pragma once

class GlobalServer;

/** @brief 注册 GlobalServer 全部区内消息（含 GameZone 转发分发） */
void GlobalInternMsgRegister(GlobalServer& server);
