/**
 * @file    ZoneGameZoneForwardMsg.h
 * @brief   ZoneServer 游戏区跨区转发入站
 *
 * 注册 innerMsgId ZONE_FORWARD（0x1803），处理区服间透传消息。
 * 消息经 Super EXT_GAMEZONE_FWD_REQ 解包后到达。
 */

#pragma once

class ZoneServer;

/**
 * @brief 注册跨区转发 handler
 * @param server ZoneServer 实例
 */
void ZoneGameZoneForwardMsgRegister(ZoneServer& server);
