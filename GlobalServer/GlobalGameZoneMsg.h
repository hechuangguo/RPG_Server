/**
 * @file    GlobalGameZoneMsg.h
 * @brief   GlobalServer 游戏区入站消息统一注册
 *
 * RegisterListen 口由 Super 连接；注册顺序：
 *   1. GameZoneMsgRegisterForwardDispatch
 *   2. GlobalGameZoneRankMsg（GLB_RANK_UPDATE）
 *   3. GlobalGameZoneSyncMsg（GLB_DATA_SYNC）
 */

#pragma once

class GlobalServer;

/**
 * @brief 注册 GlobalServer 全部游戏区入站 handler
 * @param server GlobalServer 实例
 */
void GlobalGameZoneMsgRegister(GlobalServer& server);
