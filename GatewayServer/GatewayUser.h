/**
 * @file    GatewayUser.h
 * @brief  GatewayServer 客户端会话用户 —— 继承 IUser，维护连接与登录状态
 */

#pragma once
#include "../sdk/util/UserBase.h"
#include "../sdk/net/NetDefine.h"
#include "../sdk/timer/TimerMgr.h"
#include <cstdint>
#include <unordered_set>

/**
 * @brief 客户端连接登录状态
 */
enum class ClientState : uint8_t
{
    CONNECTED   = 0, /**< TCP 已建立，待 Gateway 票据鉴权 */
    AUTHING     = 1, /**< 校验 loginToken 中 */
    ACCOUNT_OK  = 2, /**< 账号已认证，可选角/创角 */
    ENTERING    = 3, /**< 已选角，Super 进世界中 */
    IN_WORLD    = 4, /**< 已进入游戏，可转发 Scene 消息 */
};

/**
 * @brief GatewayServer 维护的客户端会话（原 ClientSession 逻辑）
 */
class GatewayUser : public IUser
{
public:
    /** @brief 构造会话并记录 connId、初始化心跳与连接时间 */
    explicit GatewayUser(ConnID connId)
        : IUser(makeBase(connId))
    {
        const uint64_t nowMs = TimerMgr::NowMs();
        lastHeartbeat = nowMs;
        connectedAtMs = nowMs;
    }

    /** @brief TCP 连接建立时刻（ms） */
    uint64_t getConnectedAtMs() const { return connectedAtMs; }

    /** @brief 发起网关票据鉴权时刻（ms）；AUTHING 超时检测用 */
    uint64_t getAuthStartedAtMs() const { return authStartedAtMs; }

    /** @brief 记录进入 AUTHING 的时间戳 */
    void setAuthStartedAtMs(uint64_t ms) { authStartedAtMs = ms; }

    /** @brief 进入 ENTERING 时刻（ms）；进世界超时检测用 */
    uint64_t getEnteringStartedAtMs() const { return enteringStartedAtMs; }

    /** @brief 记录进入 ENTERING 的时间戳 */
    void setEnteringStartedAtMs(uint64_t ms) { enteringStartedAtMs = ms; }

    /** @brief 是否已记录「连接后未鉴权超时」告警 */
    bool isAuthWarnSent() const { return authWarnSent; }

    /** @brief 标记已输出未鉴权超时告警（避免重复 WARN） */
    void setAuthWarnSent(bool sent) { authWarnSent = sent; }

    /** @brief 是否已记录 CONNECTED 态首条上行（诊断用） */
    bool isFirstUplinkLogged() const { return firstUplinkLogged; }

    /** @brief 标记 CONNECTED 态首条上行已记录 */
    void setFirstUplinkLogged(bool logged) { firstUplinkLogged = logged; }

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

    /** @brief 票据鉴权成功后的账号 ID */
    uint64_t getAccid() const { return accid; }

    /** @brief 绑定账号 ID */
    void setAccid(uint64_t id) { accid = id; }

    /** @brief 当前游戏区号 */
    uint32_t getZoneId() const { return zoneId; }

    /** @brief 设置游戏区号 */
    void setZoneId(uint32_t id) { zoneId = id; }

    /** @brief 游戏类型 */
    uint8_t getGameType() const { return gameType; }

    /** @brief 设置游戏类型 */
    void setGameType(uint8_t type) { gameType = type; }

    /** @brief 用户所在 SceneServer 实例 ID（ServerList.server_id） */
    uint32_t getSceneServerId() const { return sceneServerId; }

    /** @brief 登录调度成功后绑定 Scene 实例 */
    void setSceneServerId(uint32_t serverId) { sceneServerId = serverId; }

    /** @brief 选角时绑定的登录事务幂等键 */
    uint64_t getLoginTxnId() const { return loginTxnId; }

    /** @brief 设置登录事务幂等键 */
    void setLoginTxnId(uint64_t txnId) { loginTxnId = txnId; }

    /** @brief 缓存本账号已拉取的角色 ID（选角归属校验；创角成功后会 addOwnedRole） */
    void setOwnedRoleIds(std::unordered_set<uint64_t> ids) { ownedRoleIds = std::move(ids); }

    /** @brief 角色是否属于当前账号会话（列表未就绪时创角返回的 user_id 亦可凭此选角） */
    bool ownsRole(uint64_t roleId) const
    {
        return ownedRoleIds.find(roleId) != ownedRoleIds.end();
    }

    /** @brief 创角成功后追加角色 ID */
    void addOwnedRole(uint64_t roleId) { ownedRoleIds.insert(roleId); }

    /** @brief 角色列表快照是否可用（false 时若 ownedRoleIds 含目标 user_id 仍可选角） */
    bool isRoleListReady() const { return roleListReady; }

    /** @brief 更新角色列表快照状态 */
    void setRoleListReady(bool ready) { roleListReady = ready; }

    bool sendCmdToMe(uint8_t module, uint8_t sub, const char* data, uint16_t len);
    bool sendCmdToMe(uint16_t flatMsgId, const char* data, uint16_t len);

    void info(const char* fmt, ...);
    void debug(const char* fmt, ...);
    void warn(const char* fmt, ...);
    void error(const char* fmt, ...);

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
    uint64_t    connectedAtMs = 0;                    /**< TCP 连接建立时刻（ms） */
    uint64_t    authStartedAtMs = 0;                  /**< 进入 AUTHING 时刻（ms） */
    uint64_t    enteringStartedAtMs = 0;              /**< 进入 ENTERING 时刻（ms） */
    bool        authWarnSent = false;                   /**< 预留：CONNECTED 鉴权超时先 WARN 再踢（当前直接踢线） */
    bool        firstUplinkLogged = false;              /**< CONNECTED 态首条上行已记录 */
    uint32_t    sceneServerId = 0;                    /**< 绑定的 SceneServer 实例 ID */
    uint64_t    accid = 0;                            /**< 账号 ID（票据鉴权后） */
    uint32_t    zoneId = 0;                           /**< 游戏区号 */
    uint8_t     gameType = 0;                         /**< 游戏类型 */
    uint64_t    loginTxnId = 0;                       /**< 选角进世界事务幂等键 */
    std::unordered_set<uint64_t> ownedRoleIds;        /**< 本账号已认证角色列表 */
    bool        roleListReady = false;                /**< 是否拿到有效角色列表快照 */
};
