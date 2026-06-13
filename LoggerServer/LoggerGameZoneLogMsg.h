/**
 * @file    LoggerGameZoneLogMsg.h
 * @brief   LoggerServer 游戏区日志入站（经 Super EXT 解包）
 *
 * 与 LoggerServer::RegisterHandlers 中 LOG_WRITE_REQ 直连 handler 配合：
 *   - 直连：旧式区内服 TcpClient 发 LOG_WRITE_REQ（Super 独占外联后较少使用）
 *   - EXT 路径：GameZoneMsgRegisterForwardDispatch 解包后 innerMsgId=LOG_WRITE_REQ
 * 本模块注册 ForwardDispatch，复用同一 OnWriteLog 逻辑。
 */

#pragma once

class LoggerServer;

/**
 * @brief 注册 LoggerServer 游戏区 EXT 解包与日志 handler
 * @param server LoggerServer 实例
 */
void LoggerGameZoneMsgRegister(LoggerServer& server);
