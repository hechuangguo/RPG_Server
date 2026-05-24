/**
 * @file    GatewayUser.h
 * @brief  GatewayServer 客户端会话用户 —— 继承 IUser，维护连接与登录状态
 */

#pragma once
#include "../sdk/util/UserBase.h"
#include "../sdk/net/NetDefine.h"
#include "../sdk/timer/TimerMgr.h"
#include <cstdint>

/**
 * @brief 客户端连接登录状态
 */
enum class ClientState : uint8_t
{
    CONNECTED  = 0,  /**< TCP 已建立，未登录 */
    LOGGING    = 1,  /**< 登录验证中 */
    LOGGED_IN  = 2,  /**< 已登录 */
};

/**
 * @brief GatewayServer 维护的客户端会话（原 ClientSession 逻辑）
 */
class GatewayUser : public IUser
{
public:
    explicit GatewayUser(ConnID connId)
        : IUser(makeBase(connId))
    {
        lastHeartbeat = TimerMgr::NowMs();
    }

    ClientState getClientState() const { return clientState; }
    void setClientState(ClientState state) { clientState = state; }

    uint64_t getLastHeartbeat() const { return lastHeartbeat; }
    void touchHeartbeat() { lastHeartbeat = TimerMgr::NowMs(); }

    ConnID getConnId() const { return Base().connID; }

    void setUserId(UserID userId)
    {
        Base().userID = userId;
    }

private:
    static UserBase makeBase(ConnID connId)
    {
        UserBase base;
        base.connID = connId;
        return base;
    }

    ClientState clientState = ClientState::CONNECTED;
    uint64_t    lastHeartbeat = 0;
};
