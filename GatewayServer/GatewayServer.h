/**
 * @file    GatewayServer.h
 * @brief  网关服务器 —— 客户端 TCP 接入点、登录流程控制、消息转发
 *
 * ## 职责
 * - 面向客户端：监听公网端口，接受玩家 TCP 连接
 * - 登录流程：验证账号 → 转发给 SuperServer 调度 → 返回登录结果
 * - 消息转发：客户端 → Scene/Session（上行）；Scene 下行经出站 Scene 连接回传
 * - 心跳超时检测（60 秒无心跳自动踢除）
 *
 * ## 依赖关系
 * - 出站：SuperServer / RecordServer / SessionServer / SceneServer
 * - 入站：游戏客户端（clientPort）
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/util/Singleton.h"
#include "../sdk/util/ServerList.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../Common/LoginMsg.h"
#include "../protocal/InternalMsg.h"
#include "GatewayUser.h"
#include "GatewayUserManager.h"
#include "ClientMsgValidator.h"
#include "ClientMsgRouter.h"
#include "GatewayScenePool.h"
#include <string>
#include <vector>

/**
 * @brief GatewayServer 核心类
 *
 * m_clientServer 接受玩家连接；区内服经 TcpClient 出站连接。
 */
class GatewayServer : public INetCallback, public LazySingleton<GatewayServer>
{
    friend void GatewayInternMsgRegister(GatewayServer& server);
    friend void GatewayClientMsgRegister(GatewayServer& server);
public:
    friend class LazySingleton<GatewayServer>;
    /** @brief 获取 GatewayServer 单例指针 */
    static GatewayServer* Instance() { return &LazySingleton<GatewayServer>::Instance(); }

private:
    /** @brief 构造 GatewayServer（初始化会话管理与状态） */
    GatewayServer();

public:

    /**
     * @brief 初始化 GatewayServer
     * @param clientPort 面向客户端的监听端口（取自 ServerList 自身条目）
     * @param cfg        全局配置（提供 SuperServer 地址）
     * @param list       集群拓扑（用于解析对端地址与自身登记信息）
     * @param selfId     本进程实例编号
     * @return 成功返回 true
     */
    bool Init(uint16_t clientPort,
              const ServerConfig& cfg, const ServerList& list, uint32_t selfId);

    /** @brief 主循环 */
    void Run();

    /** @brief 向本进程接入的客户端发送消息 */
    bool sendToClient(ConnID connId, uint8_t module, uint8_t sub,
                      const char* data, uint16_t len);

    /**
     * @brief 连接建立回调
     *
     * 客户端连接建立时创建 GatewayUser 会话。
     */
    void OnConnect(ConnID id) override;

    /**
     * @brief 连接断开回调
     *
     * 客户端断开时通知 SceneServer 用户下线。
     */
    void OnDisconnect(ConnID id) override;

    /**
     * @brief 消息到达回调
     *
     * 客户端消息走 HandleClientMsg；区内服回包经 MsgDispatcher 处理。
     */
    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    /**
     * @brief 注册内部消息处理函数
     *
     * 注册内部消息的处理回调：
     * - GW_SEND_TO_CLIENT：SceneServer 发往客户端的下行消息
     * - GW_KICK_CLIENT：主动踢除客户端连接
     * - GW_USER_LOGIN_RSP：SuperServer 完成登录调度后的响应
     * - S2S_REGISTER_RSP：Super 注册成功后延迟建立区内出站
     * - SS_LOGIN_GATEWAY_WRAP_RSP：Super 转发 Login 网关注册确认
     */
    void RegisterHandlers();

    /** @brief Super 注册成功后连接 Record/Session/全部 Scene */
    void setupUpstreamClients();

    /** @brief 轮询区内出站直至就绪或超时 */
    void pollUpstreamUntilReady();

    /** @brief 经 Super 向 LoginServer 上报本网关 */
    void reportGatewayToSuper();

    /** @brief 定时经 Super 发送 LOGIN_GATEWAY_HEARTBEAT */
    void sendLoginGatewayHeartbeat();

    /** @brief 收到 Super S2S_REGISTER_RSP 后触发延迟出站（幂等） */
    void onSuperRegisterRsp(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Login 网关注册响应（Super 包装） */
    void onLoginGatewayWrapRsp(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 客户端消息处理
     *
     * 根据消息类型分发到对应处理函数：
     * - C2S_GATEWAY_AUTH_REQ → OnGatewayAuth（票据鉴权）
     * - C2S_SELECT_USER_REQ / C2S_CREATE_USER_REQ → 选角/创角
     * - C2S_HEARTBEAT → OnClientHeartbeat
     * - 场景类消息 → 转发 SceneServer
     */
    void HandleClientMsg(ConnID connID, uint8_t module, uint8_t sub,
                         const char* data, uint16_t len);

    /** @brief 向客户端回参数/状态校验错误 */
    void sendClientError(ConnID connID, ValidateResult vr);

    /** @brief Gateway 票据鉴权 */
    void OnGatewayAuth(ConnID connID, const char* data, uint16_t len);

    /** @brief 选择角色进世界 */
    void OnSelectUser(ConnID connID, const char* data, uint16_t len);

    /** @brief 创建角色 */
    void OnCreateUser(ConnID connID, const char* data, uint16_t len);

    void OnValidateTokenRsp(ConnID fromConn, const char* data, uint16_t len);
    void OnListCharactersRsp(ConnID fromConn, const char* data, uint16_t len);
    void OnCreateCharacterRsp(ConnID fromConn, const char* data, uint16_t len);

    void sendUserListToClient(ConnID clientConn, uint64_t accid, uint32_t zoneId);

    /**
     * @brief 处理客户端心跳
     *
     * 直接回包客户端 S2C_HEARTBEAT，包含客户端发来的序列号和服务器当前时间，
     * 用于客户端计算 RTT 和维持连接活跃状态。
     */
    void OnClientHeartbeat(ConnID connID, const char* data, uint16_t len);

    /**
     * @brief 处理 SuperServer 登录调度响应
     *
     * 接收 GW_USER_LOGIN_RSP 消息后的处理逻辑：
     * - 成功（rsp->code == 0）：客户端状态设为 LOGGED_IN，发送 S2C_LOGIN_RSP 和 S2C_ENTER_GAME
     * - 失败：状态恢复为 CONNECTED，返回错误信息给客户端
     */
    void OnUserLoginRsp(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief SceneServer → Gateway → Client 下行消息转发
     *
     * 接收 GW_SEND_TO_CLIENT 消息，解析内部包格式
     * [clientConnID][module][sub][data...]，提取目标客户端连接 ID 后通过 m_clientServer 下发。
     */
    void OnSendToClient(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 踢除指定客户端连接
     *
     * 接收 GW_KICK_CLIENT 消息，包体为 4 字节的 clientConnID。
     * 主动断开客户端 TCP 连接并从会话表中删除。
     */
    void OnKickClient(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 将客户端消息打包成内部消息转发给 SceneServer
     *
     * 构造 Msg_GW_ClientMsg 头部（含 clientConnID、module、sub、dataLen），
     * 后接原始客户端消息体，通过 target 发送 GW_CLIENT_MSG。
     */
    void forwardClientMsg(TcpClient& target, ConnID connID,
                          uint8_t module, uint8_t sub,
                          const char* data, uint16_t len);

    /**
     * @brief 心跳超时检测
     *
     * 每 30 秒由定时器触发一次，遍历所有客户端会话：
     * - lastHeartbeat 距当前时间超过 60 秒的视为超时
     * - 超时客户端被踢除并从会话表中删除
     */
    void CheckTimeout();

    /**
     * @brief 向 SuperServer 注册
     *
     * 500ms 后由定时器触发，发送 S2S_REGISTER_REQ 消息，
     * 告知 SuperServer 本网关的服务器类型、ID、IP 和客户端端口，
     * 用于 SuperServer 统一管理所有子服务器。
     */
    void RegisterToSuper();

    /**
     * @brief 向 SuperServer 发送心跳
     *
     * 每 10 秒由定时器触发，发送 S2S_HEARTBEAT 消息，
     * 携带递增的序列号和服务器时间戳，维持与 SuperServer 的活跃连接。
     * SuperServer 可通过此心跳判断网关是否存活。
     */
    void SendHeartbeat();
    // ======================== 成员变量 ========================
    // --- 网络服务 ---
    TcpServer m_clientServer;   /**< 入站：游戏客户端连接 */
    TcpClient m_superClient;    /**< 出站 SuperServer（注册、登录调度） */
    TcpClient m_recordClient;   /**< 出站 RecordServer（账号验证） */
    TcpClient m_sessionClient;  /**< 出站 SessionServer（社交/任务） */
    GatewayScenePool m_scenePool; /**< 出站多 SceneServer 连接池 */
    uint32_t  m_hbSeq = 0;      /**< 心跳序列号，每次发送心跳递增 */
    uint16_t  m_clientPort = 9005;   /**< 客户端监听端口 */
    ServerEntry m_self;              /**< 本进程在 ServerList 中的拓扑条目（注册上报用） */
    ServerList m_serverList;         /**< 启动期拉取的集群拓扑（延迟出站用） */
    bool m_upstreamReady = false;    /**< 是否已完成区内出站连接 */
    bool m_reportedToLogin = false;  /**< 是否已向 Login 上报网关（经 Super） */
    uint32_t m_zoneId = 1;           /**< 本游戏区号（config.xml Zone） */
    uint8_t m_gameType = 0;          /**< 游戏类型 */
    // --- 客户端管理 ---
    GatewayUserManager m_userManager;  /**< 客户端会话表（connID -> GatewayUser） */
};
