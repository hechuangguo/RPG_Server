/**
 * @file    LoggerInternMsgRegister.h
 * @brief  日志服区内 S2S 消息注册
 */

#pragma once

class LoggerServer;

/** @brief 注册日志服区内消息处理器 */
void LoggerInternMsgRegister(LoggerServer& server);
