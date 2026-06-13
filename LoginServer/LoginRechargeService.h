/**
 * @file    LoginRechargeService.h
 * @brief   充值业务骨架（首期空实现）
 *
 * 预期经 EXT_GAMEZONE_FWD 解包后的 LOGIN_RECHARGE_REQ（0x1904）调用；
 * 需回区服时经 Super SS_EXTERN_FWD_RSP 下发（待实现）。
 */

#pragma once

#include "../sdk/net/NetDefine.h"

class LoginServer;

/**
 * @brief LoginServer 充值业务处理（骨架）
 */
class LoginRechargeService
{
public:
    /**
     * @brief 构造并绑定 LoginServer
     * @param owner LoginServer 实例
     */
    explicit LoginRechargeService(LoginServer& owner);

    /**
     * @brief 处理充值请求（当前仅打日志占位）
     * @param fromConn Super 侧 RegisterListen 连接 ID
     * @param data     业务 body（待定义）
     * @param len      长度
     */
    void onRechargeReq(ConnID fromConn, const char* data, uint16_t len);

private:
    LoginServer& m_owner; /**< 所属 LoginServer */
};
