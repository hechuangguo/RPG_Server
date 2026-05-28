/**
 * @file    GatewayServer.h
 * @brief  网关服务器 —— 客户端 TCP 接入点、登录流程控制、消息转发
 *
 * ## 职责
 * - 面向客户端：监听公网端口，接受玩家 TCP 连接
 * - 面向内部：监听内网端口，接收来自 SceneServer 的下行消息
 * - 登录流程：验证账号 → 转发给 SuperServer 调度 → 返回登录结果
 * - 消息转发：客户端 → SceneServer（上行）、SceneServer → 客户端（下行）
 * - 心跳超时检测（60 秒无心跳自动踢除）
 *
 * ## 双端口设计
 * - 外网端口 (clientPort = 9005)：客户端通过公网连接，进行登录和游戏数据交互
 * - 内网端口 (innerPort = 19005)：SceneServer 等内部服务器通过内网连接，传递下行消息和踢人指令
 * - 通过 connID 大小区分客户端连接（< 100000）和内部服务器连接（>= 100000）
 *
 * ## 依赖关系
 * - 依赖 SuperServer + RecordServer + SceneServer
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../common/ClientMsg.h"
#include "../protocal/InternalMsg.h"
#include "GatewayUser.h"
#include "GatewayUserManager.h"
#include "ClientMsgValidator.h"
#include "ClientMsgRouter.h"
#include <string>
#include <vector>

/**
 * @brief GatewayServer 核心类
 *
 * 双 TcpServer：m_clientServer（客户端） + m_innerServer（内部）。
 * 通过 INetCallback::OnConnect 的 connID 区分客户端/内部连接。
 */
class GatewayServer : public INetCallback
{
public:
    /** @brief 构造 GatewayServer（初始化会话管理与状态） */
    GatewayServer();

    /**
     * @brief 初始化 GatewayServer
     * @param clientPort 面向客户端的监听端口
     * @param innerPort  面向内部服务器的监听端口
     * @param cfg        全局配置
     * @return 成功返回 true
     */
    bool Init(uint16_t clientPort, uint16_t innerPort,
              const ServerConfig& cfg);

    /** @brief 主循环 */
    void Run();

    /**
     * @brief 连接建立回调
     *
     * 通过 connID 大小区分客户端和内部连接。
     * - connID < 100000：客户端连接，创建 ClientSession 记录
     * - connID >= 100000：内部服务器连接（如 SceneServer），直接记录日志
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
     * 区分客户端消息和内部消息分别处理。
     * - 客户端连接：通过 HandleClientMsg 处理登录、心跳和游戏消息
     * - 内部连接：通过 MsgDispatcher 分发到对应的处理函数
     */
    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    /**
     * @brief 注册内部消息处理函数
     *
     * 注册四个内部消息的处理回调：
     * - GW_SEND_TO_CLIENT：SceneServer 发往客户端的下行消息
     * - GW_KICK_CLIENT：主动踢除客户端连接
     * - REC_LOGIN_VERIFY_RSP：RecordServer 的登录验证响应
     * - GW_USER_LOGIN_RSP：SuperServer 完成登录调度后的响应
     */
    void RegisterHandlers();

    /**
     * @brief 客户端消息处理
     *
     * 根据消息类型分发到对应处理函数：
     * - C2S_LOGIN_REQ  → OnClientLogin（登录验证流程，仅 CONNECTED 状态可触发）
     * - C2S_HEARTBEAT  → OnClientHeartbeat（回包服务器时间）
     * - 其他消息       → ForwardToScene（将消息转发给 SceneServer，仅 LOGGED_IN 状态可触发）
     */
    void HandleClientMsg(ConnID connID, uint8_t module, uint8_t sub,
                         const char* data, uint16_t len);

    /** @brief 向客户端回参数/状态校验错误 */
    void sendClientError(ConnID connID, ValidateResult vr);

    /**
     * @brief 处理客户端登录请求
     *
     * 收到客户端的 C2S_LOGIN_REQ 消息后：
     * 1. 将客户端状态设为 LOGGING，防止重复登录
     * 2. 提取账号密码，构造 Msg_REC_LoginVerifyReq
     * 3. 附上网关侧的 connID，发送给 RecordServer 进行验证
     */
    void OnClientLogin(ConnID connID, const char* data, uint16_t len);

    /**
     * @brief 处理客户端心跳
     *
     * 直接回包客户端 S2C_HEARTBEAT，包含客户端发来的序列号和服务器当前时间，
     * 用于客户端计算 RTT 和维持连接活跃状态。
     */
    void OnClientHeartbeat(ConnID connID, const char* data, uint16_t len);

    /**
     * @brief 处理 RecordServer 验证响应
     *
     * 接收 REC_LOGIN_VERIFY_RSP 消息后的处理逻辑：
     * - 验证失败（rsp->code != 0）：回包客户端 S2C_LOGIN_RSP 错误信息，状态恢复为 CONNECTED
     * - 验证成功（rsp->code == 0）：记录 userID，转发 GW_USER_LOGIN_REQ 给 SuperServer 完成服务器调度
     */
    void OnLoginVerifyRsp(ConnID fromConn, const char* data, uint16_t len);

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
     * 接收 GW_SEND_TO_CLIENT 消息，解析内部包格式 [clientConnID(4字节)][msgID(2字节)][data...]，
     * 提取目标客户端连接 ID 后通过 m_clientServer 发送给对应客户端。
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
     * 构造 Msg_GW_ClientMsg 头部（包含 clientConnID、msgID、dataLen），
     * 后接原始客户端消息体，通过 m_sceneClient 发送 GW_CLIENT_MSG 给 SceneServer 进行处理。
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
    TcpServer m_clientServer;   /**< 面向客户端的 TCP Server，监听外网端口 9005，接受玩家连接 */
    TcpServer m_innerServer;    /**< 面向内部服务器的 TCP Server，监听内网端口 19005，接收 SceneServer 等内部连接 */

    // --- 上游连接 ---
    TcpClient m_superClient;    /**< 到 SuperServer 的连接，用于注册和登录调度 */
    TcpClient m_recordClient;   /**< 到 RecordServer 的连接，用于账号密码验证 */
    TcpClient m_sceneClient;    /**< 到 SceneServer 的连接 */
    TcpClient m_sessionClient;  /**< 到 SessionServer 的连接（社交/任务） */

    // --- 状态 ---
    uint32_t  m_hbSeq = 0;      /**< 心跳序列号，每次发送心跳递增 */

    // --- 双端口配置 ---
    uint16_t  m_clientPort = 9005;   /**< 外网端口：客户端通过公网连接此端口进行登录和游戏数据交互 */
    uint16_t  m_innerPort  = 19005;  /**< 内网端口：内部服务器（SceneServer 等）通过内网连接此端口发送下行消息 */

    // --- 客户端管理 ---
    GatewayUserManager m_userManager;  /**< 客户端会话表（connID -> GatewayUser） */
};
