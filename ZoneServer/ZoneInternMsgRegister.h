/**
 * @file    ZoneInternMsgRegister.h
 * @brief   跨区服区内 S2S 消息注册聚合
 */

#pragma once

class ZoneServer;

/** @brief 注册 ZoneServer 全部区内消息（含 GameZone 转发分发） */
void ZoneInternMsgRegister(ZoneServer& server);
