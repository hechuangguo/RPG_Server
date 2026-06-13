/**
 * @file    ZoneGameZoneCrossMsg.h
 * @brief   ZoneServer 游戏区跨区请求入站
 *
 * 注册 innerMsgId ZONE_CROSS_REQ（0x1801），按目标 ZoneID 路由转发。
 * 消息经 Super EXT_GAMEZONE_FWD_REQ 解包后到达。
 */

#pragma once

class ZoneServer;

/**
 * @brief 注册跨区请求 handler
 * @param server ZoneServer 实例
 */
void ZoneGameZoneCrossMsgRegister(ZoneServer& server);
