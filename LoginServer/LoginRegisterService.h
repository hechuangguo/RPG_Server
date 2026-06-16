/**
 * @file    LoginRegisterService.h
 * @brief   LoginServer 客户端注册服务
 */

#pragma once

#include "../sdk/net/NetDefine.h"

class LoginServer;

/**
 * @brief ClientListen 口账号注册处理器
 */
class LoginRegisterService
{
public:
    /**
     * @brief 构造并绑定 LoginServer
     * @param owner LoginServer 实例
     */
    explicit LoginRegisterService(LoginServer& owner);

    /**
     * @brief 处理客户端注册请求
     * @param connID 客户端连接 ID
     * @param data   Msg_C2S_RegisterReq
     * @param len    长度
     */
    void onClientRegister(ConnID connID, const char* data, uint16_t len);

private:
    void sendRegisterRsp(ConnID connID, int32_t code, const char* msg, uint64_t accid = 0);

    LoginServer& m_owner;
};
