/**
 * @file    LoginAuthService.h
 * @brief   LoginServer 客户端登录（ClientListen）
 *
 * 职责：
 *   - 处理 C2S_ZONE_LIST_REQ（返回 serverlist.xml 区服列表）
 *   - 处理 C2S_LOGIN_REQ（GameUser 账号校验）
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
     * @param data   Protobuf C2SLoginReq body
     * @param len    长度
     */
    void onClientLogin(ConnID connID, const char* data, uint16_t len);

    /**
     * @brief 处理客户端区列表请求
     * @param connID 客户端连接 ID
     * @param data   Protobuf C2SZoneListReq；解析失败时首字节为 gameType 过滤
     * @param len    长度
     */
    void onClientZoneList(ConnID connID, const char* data, uint16_t len);

private:
    /**
     * @brief 下发网关地址 S2C_GATEWAY_INFO
     * @param connID   客户端连接 ID
     * @param code     0 成功，非 0 失败
     * @param msg      提示文案
     * @param zoneId   所选游戏区号（code=0 时有效）
     * @param gameType 游戏类型（code=0 时有效）
     */
    void sendGatewayInfo(ConnID connID, int32_t code, const char* msg,
                         uint32_t zoneId = 0, uint8_t gameType = 0);

    LoginServer& m_owner; /**< 所属 LoginServer */
};
