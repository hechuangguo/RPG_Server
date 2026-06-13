/**
 * @file    GameZoneMsgDispatch.h
 * @brief  独立服：解包 EXT_GAMEZONE_FWD_REQ 并分发 inner 业务消息
 */

#pragma once

#include "../net/NetDefine.h"

/** @brief 注册 EXT_GAMEZONE_FWD_REQ 处理器（独立服 Listen 口共用） */
void GameZoneMsgRegisterForwardDispatch();

/** @brief 解包并 Dispatch innerMsgId */
void GameZoneOnForwardReq(ConnID fromConn, const char* data, uint16_t len);
