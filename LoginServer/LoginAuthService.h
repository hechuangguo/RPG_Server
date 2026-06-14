/**
 * @file    LoginAuthService.h
 * @brief   LoginServer 客户端登录（ClientListen）
 *
 * 职责：
 *   - 处理 C2S_ZONE_LIST_REQ（返回 serverlist.xml 区服列表）
 *   - 处理 C2S_LOGIN_REQ（MySQL 校验 / 自动建号）
 *   - 回复 S2C_LOGIN_RSP
 *   - 成功时经 ZoneInfo + LoginGatewayRegistry 下发 S2C_GATEWAY_INFO
 */

#pragma once

#include "../sdk/net/NetDefine.h"

class LoginServer;

/**
 * @brief ClientListen 口账号登录与网关地址下发
 */
class LoginAuthService
{
public:
    /**
     * @brief 构造并绑定 LoginServer
     * @param owner LoginServer 实例（访问 clientServer、db、gatewayRegistry）
     */
    explicit LoginAuthService(LoginServer& owner);

    /**
     * @brief 处理客户端登录请求
     * @param connID 客户端连接 ID
     * @param data   Msg_C2S_LoginReq
     * @param len    长度
     */
    void onClientLogin(ConnID connID, const char* data, uint16_t len);

    /**
     * @brief 处理客户端区列表请求
     * @param connID 客户端连接 ID
     * @param data   Msg_C2S_ZoneListReq（空 body 视为 gameType=0xFF）
     * @param len    长度
     */
    void onClientZoneList(ConnID connID, const char* data, uint16_t len);

private:
    /**
     * @brief 下发网关地址 S2C_GATEWAY_INFO
     * @param connID 客户端连接 ID
     * @param code   0 成功，非 0 失败
     * @param msg    提示文案
     */
    void sendGatewayInfo(ConnID connID, int32_t code, const char* msg);

    LoginServer& m_owner; /**< 所属 LoginServer */
};
