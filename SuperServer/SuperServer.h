/**
 * @file    SuperServer.h
 * @brief  超级服务器 —— 统一管理所有子服务器连接，协调用户登录流程
 *
 * ## 职责
 * - 作为所有子服务器的注册中心（维护路由表）
 * - 管理用户登录调度（GatewayServer → RecordServer → SceneServer）
 * - 定期心跳检查（90 秒超时标记离线）
 * - 处理踢人请求（通知 GatewayServer 断开客户端）
 *
 * ## 依赖关系
 * - 自举：读 MySQL ServerList；出站 loginserverlist.xml 外联（Logger/Global/Zone）
 * - 入站：区内子进程注册（Gateway/Session/Record/AOI/Scene）
 *
 * ## 架构位置
 * @code
 *                    ┌──────────────┐
 *                    │ SuperServer  │
 *                    └──────┬───────┘
 *            ┌──────────────┼──────────────┐
 *     SessionServer   RecordServer   SceneServer ...
 * @endcode
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/UserBase.h"
#include "../sdk/util/Singleton.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/util/ServerList.h"
#include "../sdk/util/ExternalServerHub.h"
#include "../sdk/util/LoginServerList.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <mysql/mysql.h>
#include <unordered_map>
#include <string>

/**
 * @brief SuperServer 维护的子服务器信息
 *
 * 每个连接的子服务器在此结构中被记录，用于路由和心跳检测。
 * SubServerType 定义见 InternalMsg.h。
 */
struct SubServerInfo
{
    ConnID         connID;         /**< 与 SuperServer 的连接 ID */
    SubServerType  type;           /**< 服务器类型 */
    uint32_t       serverID;       /**< 服务器实例编号 */
    std::string    ip;             /**< 监听 IP */
    uint16_t       port;           /**< 监听端口 */
    std::string    name;           /**< 服务器名（注册上报，来自 ServerList） */
    bool           alive;          /**< 是否存活（心跳超时则置 false） */
    uint64_t       lastHeartbeat;  /**< 最后一次心跳时间戳（ms） */
};

/**
 * @brief SuperServer 维护的用户代理信息
 *
 * 记录用户当前所在的 GatewayServer 和 SceneServer 连接，
 * 用于踢人、跨服路由等操作。
 *
 * 字段关系说明：
 * - gatewayConnID 与 sceneConnID 分别指向用户关联的两个子服务器内部连接，
 *   二者通过 SuperServer 路由表关联，但彼此无直接依赖。
 * - gatewayClientConnID 是 GatewayServer 内部为客户端分配的连接标识，
 *   在踢人操作时需要通过 gatewayConnID 定位 GatewayServer，
 *   再通过 gatewayClientConnID 定位具体客户端连接。
 * - userID 作为唯一索引，将上述三个连接信息绑定为一个用户在线路由条目。
 */
struct UserProxy
{
    UserID   userID;              /**< 用户 ID */
    ConnID   gatewayConnID;       /**< 对应 GatewayServer 的内部连接 */
    ConnID   sceneConnID;         /**< 分配到的 SceneServer 连接 */
    uint32_t gatewayClientConnID; /**< 用户在 GatewayServer 里的客户端连接 ID */
};

/** @brief Super 登录编排阶段（PendingLogin 状态机） */
enum class LoginTxnPhase : uint8_t
{
    LOAD_USER       = 0, /**< 等待 Record 加载 */
    RESOLVE_MAP     = 1, /**< 等待 Session 解析地图 */
    ENTER_SCENE     = 2, /**< 等待 Scene 入场确认 */
};

/** @brief 登录流程中的待完成上下文 */
struct PendingLogin
{
    UserID       userID;               /**< 用户 ID（作为 pending key） */
    ConnID       gatewayConnID;        /**< 发起登录的 Gateway 连接 */
    uint32_t     gatewayClientConnID;  /**< Gateway 内客户端连接 ID */
    uint64_t     loginTxnId = 0;       /**< 幂等键（与客户端选角一致） */
    ConnID       sceneConnID = INVALID_CONN_ID; /**< 解析 map 后分配的 Scene 连接 */
    uint32_t     mapId = 0;            /**< 用户目标地图 ID */
    LoginTxnPhase phase = LoginTxnPhase::LOAD_USER; /**< 当前编排阶段 */
    bool         awaitingMapResolve = false; /**< 等待 Session 解析 sceneServerId */
    uint64_t     startedAtMs = 0;      /**< 事务开始时间（幂等/超时） */
    UserBaseWire userData{};           /**< Record 返回的用户基础数据 */
};

/**
 * @brief SuperServer 核心类
 *
 * 实现 INetCallback，通过 TcpServer 监听所有子服务器的 TCP 长连接。
 * 单例模式运行（一个游戏区仅一个进程）。
 */
class SuperServer : public INetCallback, public LazySingleton<SuperServer>
{
    friend void SuperInternMsgRegister(SuperServer& server);
public:
    friend class LazySingleton<SuperServer>;
    /** @brief 获取 SuperServer 单例指针 */
    static SuperServer* Instance() { return &LazySingleton<SuperServer>::Instance(); }

private:
    /** @brief 构造 SuperServer 实例（初始化路由与定时器容器） */
    SuperServer();

public:
    /** @brief 析构：关闭 ServerList 只读用的 MySQL 连接 */
    ~SuperServer() override;

    /**
     * @brief 初始化 SuperServer
     * @param ip   监听 IP
     * @param port 监听端口
     * @param cfg  全局配置（提供 MySQL 连接信息，用于只读加载 ServerList）
     * @return 成功返回 true
     *
     * 启动期直连 MySQL 只读加载 ServerList（集群拓扑），注册消息处理函数，
     * 启动 30 秒间隔心跳检查定时器。
     */
    bool Init(const std::string& ip, uint16_t port, const ServerConfig& cfg);

    /** @brief 主循环：轮询网络事件 + 驱动定时器 */
    void Run();

    /** @brief 按 loginserverlist.xml 连接外联 Logger/Global/Zone/Login（仅 Super 使用） */
    void setupExternalClients(const LoginServerList& list);

    /** @brief 外联 Hub（SuperExternRouter 使用） */
    ExternalServerHub& externHub() { return m_externHub; }

    /** @brief 区内服入站 TcpServer */
    TcpServer& tcpServer() { return m_server; }

    /** @brief 查找存活区内服连接 */
    ConnID findSubServerConn(SubServerType type) { return findSubServer(type); }

    /**
     * @brief 周期汇总 Gateway 在线人数并上报 LoginServer
     *
     * 由 SuperZoneStatusMsg 定时器触发。
     */
    void reportZoneStatusToLogin();

    /** @brief 本游戏区号（config.xml Zone） */
    uint32_t zoneId() const { return m_zoneId; }

    /** @brief 游戏类型（config.xml Zone） */
    uint8_t gameType() const { return m_gameType; }

    // ============================================================
    //  INetCallback 实现
    // ============================================================
    /** @brief 子服务器 TCP 连接建立 */
    void OnConnect(ConnID id) override;

    /**
     * @brief 子服务器断开
     *
     * 移除该服务器的路由表记录，后续 findSubServer 将返回 INVALID_CONN_ID。
     */
    void OnDisconnect(ConnID id) override;

    /** @brief 消息到达后派发给 MsgDispatcher */
    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    /**
     * @brief 注册所有消息处理函数
     *
     * S2S_REGISTER_REQ → onRegister（注册）
     * S2S_HEARTBEAT    → onHeartbeat（心跳）
     * GW_USER_LOGIN_REQ→ onUserLoginReq（登录调度）
     * SS_KICK_USER     → onKickUser（踢人）
     */
    void registerHandlers();

    /**
     * @brief 处理子服务器注册
     *
     * 收到 Msg_S2S_Register 后记录服务器信息到 m_servers，
     * 并回复 S2S_REGISTER_RSP 确认。
     */
    void onRegister(ConnID connID, const Msg_S2S_Register& req);

    /**
     * @brief 处理心跳
     *
     * 更新 lastHeartbeat 时间戳并回复 ACK（含服务器时间）。
     */
    void onHeartbeat(ConnID connID, const char* data, uint16_t len);

    /**
     * @brief 处理子服务器的 ServerList 拉取请求
     *
     * 收到 S2S_SERVERLIST_REQ 后，将缓存的 m_serverList 全量条目打包为
     * S2S_SERVERLIST_RSP（count + count×Msg_ServerEntry）回发请求方。
     */
    void onServerListReq(ConnID connID, const char* data, uint16_t len);

    /**
     * @brief 启动期直连 MySQL 只读加载 ServerList 到 m_serverList
     * @param cfg 提供 host/port/user/pass/name 等连接信息
     * @return 连接并查询成功返回 true
     *
     * @note 放宽“仅 RecordServer 连库”红线：此处仅启动期只读，写库仍只在 Record。
     */
    bool loadServerList(const ServerConfig& cfg);

    /**
     * @brief 处理用户登录请求
     *
     * GatewayServer 选角后进世界时发送 Msg_GW_UserEnterReq，
     * SuperServer 负责重复登录踢线、加载用户、解析地图并调度 SceneServer。
     */
    void onUserLoginReq(ConnID connID, const Msg_GW_UserEnterReq& req);

    /** @brief 处理 RecordServer 的用户加载返回，向 Session 解析 map 后触发入场 */
    void onLoadUserRsp(ConnID connID, const char* data, uint16_t len);

    /** @brief Session 返回 mapId 对应的 sceneServerId 后继续入场 */
    void onResolveMapRsp(ConnID connID, const Msg_SES_ResolveMapRsp& rsp);

    /** @brief 加载与 map 解析完成后向 Scene 发送 SCE_USER_ENTER_REQ */
    void sendUserEnterToScene(PendingLogin& pending);

    /** @brief 处理 SceneServer 入场返回，给 Gateway 回登录最终结果 */
    void onUserEnterRsp(ConnID connID, const Msg_SCE_UserEnterRsp& rsp);

    /** @brief 向 Gateway 回登录失败（调度/加载/入场任一步失败） */
    void sendLoginFailToGateway(ConnID gatewayConnID, uint32_t clientConnID, int32_t code);

    /** @brief 重复登录时踢除旧会话（Gateway 断连 + Scene 离场） */
    void kickExistingUserSession(UserID userID);

    /** @brief 清理超时 pending 登录并回包失败 */
    void checkPendingLoginTimeouts();

    /**
     * @brief 处理踢人请求
     *
     * 通知对应 GatewayServer 踢除指定用户的客户端连接。
     */
    void onKickUser(ConnID connID, const char* data, uint16_t len);

    /**
     * @brief 定期心跳检查
     *
     * 遍历所有子服务器，超过 90 秒未收到心跳则标记为离线。
     *
     * 心跳超时处理流程：
     * 1. 获取当前时间戳（毫秒）。
     * 2. 遍历 m_servers 路由表中的所有子服务器条目。
     * 3. 对每个子服务器，计算当前时间与 lastHeartbeat 的差值。
     * 4. 若差值超过 90000ms（90 秒），则将该服务器的 alive 标记为 false。
     * 5. 日志记录超时服务器的 connID 和类型，便于运维排查。
     * 6. 注意：此处仅标记离线（软断开），不主动关闭 TCP 连接。连接的实际
     *    断开由底层网络事件触发 OnDisconnect 回调处理。标记为离线的服务器
     *    将不会被 findSubServer 选中，从而实现故障隔离。
     */
    void checkHeartbeat();

    /**
     * @brief 查找指定类型的子服务器连接
     * @param type 服务器类型
     * @return 找到的连接 ID，未找到返回 INVALID_CONN_ID
     */
    ConnID findSubServer(SubServerType type);

    /**
     * @brief 按 serverID 查找指定类型的子服务器连接
     * @param type 服务器类型
     * @param serverId ServerList 中的 server_id
     */
    ConnID findSubServerByServerId(SubServerType type, uint32_t serverId);

    /**
     * @brief 选择 SceneServer（负载均衡策略）
     *
     * 负载均衡策略详细说明：
     * 当前采用"首个存活"策略（First-Alive），遍历 m_servers 路由表，
     * 返回第一个类型为 SCENE 且 alive == true 的服务器连接。
     *
     * 策略特点：
     * - 实现简单，无需额外统计信息，适合固定数量的 SceneServer 部署。
     * - 所有登录请求持续分配到同一台 SceneServer，直至该服务器离线。
     * - 当前选中服务器离线后自动切换到路由表中下一个存活的 SceneServer。
     *
     * 可扩展方向（TODO）：
     * - 轮询（Round-Robin）：在多个存活 SceneServer 间轮流分配，均匀分摊负载。
     * - 最少连接（Least-Connections）：优先选择当前在线用户数最少的 SceneServer。
     * - 哈希取模（Hash）：根据 userID 取模固定分配到指定服务器，适合有状态场景。
     * - 加权分配（Weighted）：根据各 SceneServer 的硬件配置（CPU/内存）赋予不同权重。
     *
     * @return 选中的 SceneServer 连接 ID，无可用服务器返回 INVALID_CONN_ID
     */
    ConnID findSceneServer();

    /** @brief 从路由表中删除指定连接 */
    void removeSubServer(ConnID connID);
    TcpServer m_server;  /**< 监听所有子服务器的 TCP Server */
    /** @brief 子服务器路由表：connID → 服务器信息 */
    std::unordered_map<ConnID, SubServerInfo> m_servers;
    /** @brief 在线用户路由表：userID → 代理信息 */
    std::unordered_map<UserID, UserProxy>     m_users;
    /** @brief 登录中的用户：userID → 待完成上下文 */
    std::unordered_map<UserID, PendingLogin>  m_pendingLogins;
    /** @brief 启动期只读加载的集群拓扑（下发给子服务器） */
    ServerList m_serverList;
    /** @brief 只读 ServerList 用的 MySQL 连接句柄（仅启动期使用） */
    MYSQL* m_db = nullptr;
    /** @brief 外联服出站连接（Logger 等） */
    ExternalServerHub m_externHub;
    uint32_t m_zoneId = 1;   /**< 本游戏区号 */
    uint8_t m_gameType = 0;  /**< 游戏类型 */
    /** @brief Gateway serverID → 最近心跳上报的在线人数 */
    std::unordered_map<uint32_t, uint32_t> m_gatewayOnline;
};
