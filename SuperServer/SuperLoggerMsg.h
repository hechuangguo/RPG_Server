/**
 * @file    SuperLoggerMsg.h
 * @brief  SuperServer Logger 外联消息注册
 */

#pragma once

class SuperServer;

/** @brief 注册 Logger 相关 Super 侧 handler（EXT 回包等） */
void SuperLoggerMsgRegister(SuperServer& super);
