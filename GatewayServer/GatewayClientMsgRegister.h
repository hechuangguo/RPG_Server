/**
 * @file    GatewayClientMsgRegister.h
 * @brief  网关服客户端 LOCAL 消息注册
 */

#pragma once

class GatewayServer;

/** @brief 注册网关本地处理的客户端消息（经 Validator/Router LOCAL 路径） */
void GatewayClientMsgRegister(GatewayServer& server);
