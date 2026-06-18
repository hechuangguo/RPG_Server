/**
 * @file    RecordInternMsgRegister.h
 * @brief  存档服区内 S2S 消息注册
 */

#pragma once

class RecordServer;

/** @brief 注册存档服区内消息处理器 */
void RecordInternMsgRegister(RecordServer& server);
