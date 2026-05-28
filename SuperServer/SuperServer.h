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
 * - 不依赖任何其他服务器（最先启动）
 * - 被所有其他服务器依赖（通过 TcpClient 连接注册）
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
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
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

/** @brief 登录流程中的待完成上下文 */
struct PendingLogin
{
    UserID       userID;               /**< 用户 ID（作为 pending key） */
    ConnID       gatewayConnID;        /**< 发起登录的 Gateway 连接 */
    uint32_t     gatewayClientConnID;  /**< Gateway 内客户端连接 ID */
    ConnID       sceneConnID;          /**< 预分配的 SceneServer 连接 */
    UserBaseWire userData{};           /**< Record 返回的用户基础数据 */
};

/**
 * @brief SuperServer 核心类
 *
 * 实现 INetCallback，通过 TcpServer 监听所有子服务器的 TCP 长连接。
 * 单例模式运行（一个游戏区仅一个进程）。
 */
class SuperServer : public INetCallback
{
public:
    /** @brief 构造 SuperServer 实例（初始化路由与定时器容器） */
    SuperServer();

    /**
     * @brief 初始化 SuperServer
     * @param ip   监听 IP
     * @param port 监听端口
     * @return 成功返回 true
     *
     * 注册消息处理函数，启动 30 秒间隔心跳检查定时器。
     */
    bool Init(const std::string& ip, uint16_t port);

    /** @brief 主循环：轮询网络事件 + 驱动定时器 */
    void Run();

    // ============================================================
    //  INetCallback 实现
    // ============================================================

    void OnConnect(ConnID id) override;

    /**
     * @brief 子服务器断开
     *
     * 移除该服务器的路由表记录，后续 FindSubServer 将返回 INVALID_CONN_ID。
     */
    void OnDisconnect(ConnID id) override;

    /** @brief 消息到达后派发给 MsgDispatcher */
    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    /**
     * @brief 注册所有消息处理函数
     *
     * S2S_REGISTER_REQ → OnRegister（注册）
     * S2S_HEARTBEAT    → OnHeartbeat（心跳）
     * GW_USER_LOGIN_REQ→ OnUserLoginReq（登录调度）
     * SS_KICK_USER     → OnKickUser（踢人）
     */
    void RegisterHandlers();

    /**
     * @brief 处理子服务器注册
     *
     * 收到 Msg_S2S_Register 后记录服务器信息到 m_servers，
     * 并回复 S2S_REGISTER_RSP 确认。
     */
    void OnRegister(ConnID connID, const char* data, uint16_t len);

    /**
     * @brief 处理心跳
     *
     * 更新 lastHeartbeat 时间戳并回复 ACK（含服务器时间）。
     */
    void OnHeartbeat(ConnID connID, const char* data, uint16_t len);

    /**
     * @brief 处理用户登录请求
     *
     * GatewayServer 验证账号密码后将结果发给 SuperServer，
     * SuperServer 负责分配 SceneServer 并通知 RecordServer 加载用户数据。
     */
    void OnUserLoginReq(ConnID connID, const char* data, uint16_t len);

    /** @brief 处理 RecordServer 的用户加载返回，继续触发入场流程 */
    void OnLoadUserRsp(ConnID connID, const char* data, uint16_t len);

    /** @brief 处理 SceneServer 入场返回，给 Gateway 回登录最终结果 */
    void OnUserEnterRsp(ConnID connID, const char* data, uint16_t len);

    /** @brief 向 Gateway 回登录失败（调度/加载/入场任一步失败） */
    void SendLoginFailToGateway(ConnID gatewayConnID, uint32_t clientConnID, int32_t code);

    /**
     * @brief 处理踢人请求
     *
     * 通知对应 GatewayServer 踢除指定用户的客户端连接。
     */
    void OnKickUser(ConnID connID, const char* data, uint16_t len);

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
     *    将不会被 FindSubServer 选中，从而实现故障隔离。
     */
    void CheckHeartbeat();

    /**
     * @brief 查找指定类型的子服务器连接
     * @param type 服务器类型
     * @return 找到的连接 ID，未找到返回 INVALID_CONN_ID
     */
    ConnID FindSubServer(SubServerType type);

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
    ConnID FindSceneServer();

    /** @brief 从路由表中删除指定连接 */
    void RemoveSubServer(ConnID connID);

    TcpServer m_server;  /**< 监听所有子服务器的 TCP Server */

    /** @brief 子服务器路由表：connID → 服务器信息 */
    std::unordered_map<ConnID, SubServerInfo> m_servers;
    /** @brief 在线用户路由表：userID → 代理信息 */
    std::unordered_map<UserID, UserProxy>     m_users;
    /** @brief 登录中的用户：userID → 待完成上下文 */
    std::unordered_map<UserID, PendingLogin>  m_pendingLogins;
};
