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
    /** @brief 构造会话并记录 connId、初始化心跳时间 */
    explicit GatewayUser(ConnID connId)
        : IUser(makeBase(connId))
    {
        lastHeartbeat = TimerMgr::NowMs();
    }

    /** @brief 当前客户端登录状态 */
    ClientState getClientState() const { return clientState; }

    /** @brief 更新登录状态机 */
    void setClientState(ClientState state) { clientState = state; }

    /** @brief 最近一次心跳时间戳（ms） */
    uint64_t getLastHeartbeat() const { return lastHeartbeat; }

    /** @brief 刷新心跳时间为当前时刻 */
    void touchHeartbeat() { lastHeartbeat = TimerMgr::NowMs(); }

    /** @brief Gateway 侧 TCP 连接 ID */
    ConnID getConnId() const { return Base().connID; }

    /** @brief 登录成功后绑定 userId */
    void setUserId(UserID userId)
    {
        Base().userID = userId;
    }

    /** @brief 用户所在 SceneServer 实例 ID（ServerList.server_id） */
    uint32_t getSceneServerId() const { return sceneServerId; }

    /** @brief 登录调度成功后绑定 Scene 实例 */
    void setSceneServerId(uint32_t serverId) { sceneServerId = serverId; }

private:
    /** @brief 为 GatewayUser 构造最小 UserBase（仅 connID） */
    static UserBase makeBase(ConnID connId)
    {
        UserBase base;
        base.connID = connId;
        return base;
    }
    ClientState clientState = ClientState::CONNECTED; /**< 会话状态机状态 */
    uint64_t    lastHeartbeat = 0;                    /**< 最近心跳时间戳（ms） */
    uint32_t    sceneServerId = 0;                    /**< 绑定的 SceneServer 实例 ID */
};
