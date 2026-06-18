/**
 * @file    GatewayInternMsgRegister.h
 * @brief  网关服区内 S2S 消息注册
 */

#pragma once

class GatewayServer;

/** @brief 注册网关服区内消息处理器 */
void GatewayInternMsgRegister(GatewayServer& server);
