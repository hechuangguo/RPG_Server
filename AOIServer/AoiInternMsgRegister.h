/**
 * @file    AoiInternMsgRegister.h
 * @brief  视野服区内 S2S 消息注册
 */

#pragma once

class AOIServer;

/** @brief 注册视野服区内消息处理器 */
void AoiInternMsgRegister(AOIServer& server);
