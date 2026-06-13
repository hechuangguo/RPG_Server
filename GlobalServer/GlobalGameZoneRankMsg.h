/**
 * @file    GlobalGameZoneRankMsg.h
 * @brief   GlobalServer 游戏区排行榜更新入站
 *
 * 注册 innerMsgId GLB_RANK_UPDATE（0x1702），更新内存排行榜 Top100。
 * 消息经 Super EXT_GAMEZONE_FWD_REQ 解包后到达。
 */

#pragma once

class GlobalServer;

/**
 * @brief 注册排行更新 handler
 * @param server GlobalServer 实例
 */
void GlobalGameZoneRankMsgRegister(GlobalServer& server);
