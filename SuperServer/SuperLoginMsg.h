/**
 * @file    SuperLoginMsg.h
 * @brief  SuperServer Login 外联消息（网关注册代理）
 */

#pragma once

#include "../sdk/net/NetDefine.h"

class SuperServer;

/** @brief 注册 Login 网关代理消息 */
void SuperLoginMsgRegister(SuperServer& super);

/** @brief Gateway 网关注册包装请求 */
void SuperLoginOnGatewayWrapReq(SuperServer& super, ConnID fromConn,
                                const char* data, uint16_t len);

/** @brief Login 网关注册响应（Super 收到后转 Gateway） */
void SuperLoginOnGatewayRegisterRsp(SuperServer& super, ConnID fromLoginConn,
                                    const char* data, uint16_t len);

/** @brief Gateway 心跳经 Super 转发 Login */
void SuperLoginOnGatewayHeartbeat(SuperServer& super, ConnID fromConn,
                                  const char* data, uint16_t len);
