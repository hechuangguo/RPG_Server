/**
 * @file    LoginExternOutbox.h
 * @brief   SuperServer → LoginServer 注册口出站串行队列
 *
 * 所有 LOGIN_* 外联消息经本队列在 poll 后单帧逐条发送；外联 TLS 闪断时在途
 * 票据校验退回队列头，预热后重发（勿立即 fail Record）。
 */

#pragma once

#include "../sdk/net/NetDefine.h"
#include "../protocal/InternalMsg.h"

class SuperServer;

/**
 * @brief 登录外联出站聚合（单例式模块级队列）
 */
namespace LoginExternOutbox
{

/**
 * @brief 入队票据校验（Record→Super 收到后调用，下一帧发送）
 * @param super 队列满时向 Record 回失败包
 * @return 入队成功 true；队列满已 fail Record 时 false
 */
bool enqueueVerifyToken(SuperServer& super, ConnID recordConn,
                        const Msg_Login_VerifyTokenReq& req);

/**
 * @brief 入队网关注册转发
 * @param super SuperServer（队列满时回失败包）
 * @param gatewayWrapConn Gateway 在 Super 侧的 conn
 */
void enqueueGatewayRegister(SuperServer& super, ConnID gatewayWrapConn,
                            const Msg_Login_GatewayRegister& body);

/** @brief Login 票据校验回包到达后转发 Record */
void completeVerifyRsp(SuperServer& super, const Msg_Login_VerifyTokenRsp& rsp);

/** @brief 入队网关心跳（原始包体） */
void enqueueGatewayHeartbeat(const char* data, uint16_t len);

/** @brief 入队最近角色回填 */
void enqueueUpdateLastUser(const char* data, uint16_t len);

/** @brief 入队区状态上报 */
void enqueueZoneStatusReport(const Msg_Login_ZoneStatusReport& report);

/** @brief 入队 EXT_GAMEZONE_FWD 信封（区内经 SS_EXTERN_FWD 转发到 Login 的路径） */
void enqueueExternGameZoneFwd(const char* data, uint16_t len);

/** @brief 是否存在未完成的票据校验（在途或队列中） */
bool hasPendingVerify();

/**
 * @brief 每帧 poll 外联后调用：预热、断线重排队、刷新出站、在途超时
 *
 * 外联 TLS 闪断时不立即 fail Record，在途校验退回队列头等待重连+预热后重发。
 */
void onExternTick(SuperServer& super);

} // namespace LoginExternOutbox
