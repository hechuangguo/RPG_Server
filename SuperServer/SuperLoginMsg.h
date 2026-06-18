/**
 * @file    SuperLoginMsg.h
 * @brief   SuperServer Login 外联消息（网关注册代理）
 *
 * 代理链路（Gateway 不直连 Login）：
 *   - Gateway → Super：SS_LOGIN_GATEWAY_WRAP_REQ（0x1F14）
 *   - Super → Login：LOGIN_GATEWAY_REGISTER_REQ（0x1901）
 *   - Login → Super：LOGIN_GATEWAY_REGISTER_RSP（0x1902）
 *   - Super → Gateway：SS_LOGIN_GATEWAY_WRAP_RSP（0x1F15）
 *   - Gateway → Super → Login：LOGIN_GATEWAY_HEARTBEAT（0x1903）
 * 其它 Login 业务（充值/GM）走 SuperExternRouter SS_EXTERN_FWD。
 */

#pragma once

#include "../sdk/net/NetDefine.h"

class SuperServer;

/**
 * @brief 注册 Login 网关代理消息（WRAP / HEARTBEAT / REGISTER_RSP）
 * @param super SuperServer 实例
 */
void SuperLoginMsgRegister(SuperServer& super);

/**
 * @brief 处理 SS_LOGIN_GATEWAY_WRAP_REQ，转发 LOGIN_GATEWAY_REGISTER_REQ 到 Login
 * @param super    SuperServer 实例
 * @param fromConn Gateway 在 Super 侧的 connID
 * @param data     Msg_SS_LoginGatewayWrap
 * @param len      长度
 */
void superLoginOnGatewayWrapReq(SuperServer& super, ConnID fromConn,
                                const char* data, uint16_t len);

/**
 * @brief 处理 LOGIN_GATEWAY_REGISTER_RSP，转 SS_LOGIN_GATEWAY_WRAP_RSP 到 Gateway
 * @param super         SuperServer 实例
 * @param fromLoginConn Login 外联连接 ID
 * @param data          Msg_Login_GatewayRegisterRsp
 * @param len           长度
 */
void superLoginOnGatewayRegisterRsp(SuperServer& super, ConnID fromLoginConn,
                                    const char* data, uint16_t len);

/**
 * @brief 处理 Gateway 发来的 LOGIN_GATEWAY_HEARTBEAT，原样转发 Login
 * @param super    SuperServer 实例
 * @param fromConn Gateway 在 Super 侧的 connID
 * @param data     Msg_Login_GatewayRegister 心跳体
 * @param len      长度
 */
void superLoginOnGatewayHeartbeat(SuperServer& super, ConnID fromConn,
                                  const char* data, uint16_t len);

/** @brief 转发 Record→Login 票据校验请求 */
void superLoginOnVerifyTokenReq(SuperServer& super, ConnID fromConn,
                                const char* data, uint16_t len);

/** @brief 回传 Login→Record 票据校验响应 */
void superLoginOnVerifyTokenRsp(SuperServer& super, ConnID fromLoginConn,
                                const char* data, uint16_t len);

/** @brief 转发 Record→Login 最近角色回填请求 */
void superLoginOnUpdateLastUserReq(SuperServer& super, ConnID fromConn,
                                   const char* data, uint16_t len);
