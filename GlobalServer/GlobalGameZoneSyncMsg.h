/**
 * @file    GlobalGameZoneSyncMsg.h
 * @brief   GlobalServer 游戏区全服数据同步入站
 *
 * 注册 innerMsgId GLB_DATA_SYNC（0x1701），接收区服上报并广播给已连接区内服。
 * 消息经 Super EXT_GAMEZONE_FWD_REQ 解包后到达。
 */

#pragma once

class GlobalServer;

/**
 * @brief 注册全服同步 handler
 * @param server GlobalServer 实例
 */
void GlobalGameZoneSyncMsgRegister(GlobalServer& server);
