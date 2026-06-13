/**
 * @file    ZoneGameZoneMsg.h
 * @brief   ZoneServer 游戏区入站消息统一注册
 *
 * Listen 口由 Super 连接；注册顺序：
 *   1. GameZoneMsgRegisterForwardDispatch
 *   2. ZoneGameZoneCrossMsg（ZONE_CROSS_REQ）
 *   3. ZoneGameZoneForwardMsg（ZONE_FORWARD）
 */

#pragma once

class ZoneServer;

/**
 * @brief 注册 ZoneServer 全部游戏区入站 handler
 * @param server ZoneServer 实例
 */
void ZoneGameZoneMsgRegister(ZoneServer& server);
