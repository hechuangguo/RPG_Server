/**
 * @file    LoginGmService.h
 * @brief   GM 业务骨架（首期空实现）
 *
 * 预期经 EXT_GAMEZONE_FWD 解包后的 LOGIN_GM_CMD_REQ（0x1905）调用；
 * 需下发区服时经 Super SS_EXTERN_FWD 转发（待实现）。
 */

#pragma once

#include "../sdk/net/NetDefine.h"

class LoginServer;

/**
 * @brief LoginServer GM 指令处理（骨架）
 */
class LoginGmService
{
public:
    /**
     * @brief 构造并绑定 LoginServer
     * @param owner LoginServer 实例
     */
    explicit LoginGmService(LoginServer& owner);

    /**
     * @brief 处理 GM 指令请求（当前仅打日志占位）
     * @param fromConn Super 侧 RegisterListen 连接 ID
     * @param data     业务 body（待定义）
     * @param len      长度
     */
    void onGmCmdReq(ConnID fromConn, const char* data, uint16_t len);

private:
    LoginServer& m_owner; /**< 所属 LoginServer */
};
