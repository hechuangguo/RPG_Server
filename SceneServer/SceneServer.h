/**
 * @file    SceneServer.h
 * @brief  场景服务器 —— 在线用户数据管理、地图逻辑、Lua 脚本执行
 *
 * ## 职责
 * - 管理用户在线状态与地图实例
 * - 处理客户端游戏消息（移动、聊天、技能、心跳）
 * - 内嵌 Lua 虚拟机执行游戏脚本（NPC、任务、技能等）
 * - 与 AOIServer 协同管理视野
 *
 * ## 依赖关系
 * - 出站：SuperServer / SessionServer / RecordServer / AOIServer
 * - 入站：GatewayServer（GW_CLIENT_MSG / 下行回包）、SessionServer（副本指令等）
 * - 可选经 loginserverlist.xml 连接外联 GlobalServer / ZoneServer
 * - 支持多进程负载均衡（多 SceneServer 承载不同地图）
 *
 * ## Lua 集成
 * LuaManager 负责虚拟机与 C++→Lua 调用；ScriptFun 注册 Lua→C++ 接口。
 * - log_info(msg)     : 输出日志
 * - send_to_user(userID, msgID, data) : 向客户端发送消息
 * - SceneEntry userdata : entry:getEntryId() 等
 *
 * Lua 回调约定：
 * - OnUserEnter(userID, mapID) : 用户进入场景
 * - OnUserLeave(userID)         : 用户离开场景
 * - OnTick(nowMs)               : 每帧回调
 * - OnSkillReq(connID, data)    : 技能请求
 * - OnMsg_XXXX(connID, data)    : 自定义消息处理（XXXX 为十六进制 msgID）
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/SceneInfoLoader.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include "../common/ClientMsg.h"
#include "../sdk/util/UserWireUtil.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/util/Singleton.h"
#include "../sdk/util/ServerList.h"
#include "../sdk/util/ExternalServerHub.h"
#include "../sdk/util/LoginServerList.h"
#include "SceneUser.h"
#include "SceneNpc.h"
#include "Scene.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

/**
 * @brief SceneServer 核心类
 *
 * 连接最复杂的服务器，与几乎所有其他服务器有通信关系。
 */
class SceneServer : public INetCallback, public LazySingleton<SceneServer>
{
public:
    friend class LazySingleton<SceneServer>;
    /** @brief 获取 SceneServer 单例指针 */
    static SceneServer* Instance() { return &LazySingleton<SceneServer>::Instance(); }

private:
    /** @brief 构造 SceneServer 单例（仅 LazySingleton 调用） */
    SceneServer();

public:
    ~SceneServer() = default;

    /** @brief 供 ScriptFun 向客户端发消息 */
    void sendToClient(uint32_t clientConnId, uint16_t msgId,
                      const char* data, uint16_t len)
    {
        SendToClient(clientConnId, msgId, data, len);
    }

    /**
     * @brief 初始化 SceneServer
     * @param ip        监听 IP
     * @param port      监听端口（取自 ServerList 自身条目）
     * @param cfg       全局配置（提供 SuperServer 地址）
     * @param sceneInfo 场景地图配置
     * @param list      集群拓扑（用于解析对端地址与自身登记信息）
     * @param selfId    本进程实例编号
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg, const SceneServerInfo& sceneInfo,
              const ServerList& list, uint32_t selfId);

    /** @brief 主循环：轮询所有连接的 epoll + 驱动定时器 */
    void Run();

    /** @brief 连接外联 Logger / Global / Zone（loginserverlist.xml） */
    void setupExternalClients(const LoginServerList& list);

    /** @brief NPC 进入 AOI（创建/复活时由 SceneNpc 调用） */
    void notifyNpcEnterAoi(const SceneNpc& npc) { sendAoiEnter(npc, 1); }

    /** @brief NPC 离开 AOI（死亡/销毁时由 SceneNpc 调用） */
    void notifyNpcLeaveAoi(EntryID npcId) { sendAoiLeave(npcId); }

    /** @brief 请求 SessionServer 创建副本（异步，结果见 OnCopyCreateRsp） */
    void requestCreateCopy(CopyType copyType, uint32_t mapId, uint64_t ownerId,
                           const std::string& mapName, const std::string& mapFile,
                           uint32_t maxPlayer = 5);

    /** @brief 内部连接建立 */
    void OnConnect(ConnID id) override;

    /** @brief 内部连接断开 */
    void OnDisconnect(ConnID id) override;

    /** @brief 收到消息后派发给 MsgDispatcher */
    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    /** @brief 注册 SceneServer 的内部消息处理器 */
    void RegisterHandlers();

    /**
     * @brief 用户进入场景
     *
     * 创建 SceneUser → 加入地图 → 通知 AOI → 回包 GatewayServer → 调用 Lua OnUserEnter。
     */
    void OnUserEnter(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 回 Gateway 的入场结果包 */
    void SendUserEnterRsp(const Msg_SCE_UserEnterReq* req, int32_t code);

    /** @brief 向新进入玩家补发当前地图已有玩家可见实体 */
    void NotifyExistingPlayersOnEnter(const SceneUser& entering);

    /** @brief 用户离开场景（通知 AOI → 保存到 RecordServer → 调用 Lua → 清理内存） */
    void OnUserLeave(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 将 Scene 在线数据转发 RecordServer 写入 CharBase */
    void sendCharBaseToRecord(const SceneUser& user);

    /** @brief 处理 GatewayServer 转发来的客户端消息 */
    void OnClientMsg(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 处理 AOI 视野变化通知
     *
     * 根据消息长度区分两种场景：
     * - 若 len == sizeof(Msg_AOI_Move)：为移动坐标更新，直接同步实体位置并向同地图广播移动通知；
     * - 否则为视野进入/离开通知，解析 entityID + enter 标志位：
     *   - enter=true：向视野内其他用户广播 SpawnEntity（新实体出现）；
     *   - enter=false：向视野内其他用户广播 DespawnEntity（实体消失）。
     */
    void OnViewNotify(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 客户端消息分发路由
     *
     * 采用 switch-case 路由策略：已知协议号直接映射到对应的处理函数，
     * 未知协议号统一委派给 Lua 脚本处理（OnMsg_XXXX）。
     * 路由优先级：C++ 原生处理 > Lua 脚本扩展处理。
     */
    void HandleClientMsg(uint32_t clientConnID, uint8_t module, uint8_t sub,
                         const char* data, uint16_t len);

    /**
     * @brief 处理移动请求：坐标验证 → 位置更新 → 通知 AOI
     *
     * 移动验证流程：
     * 1. 根据消息中的 userID 查找在线用户，不存在则忽略；
     * 2. 将客户端上报的坐标 (x, y, z) 更新到用户的基础数据中；
     * 3. 构造 AOI 移动消息并转发至 AOIServer，由 AOIServer 计算视野变化
     *    并回传 OnViewNotify，触发同地图其他客户端的坐标同步。
     *
     * @note 当前未做坐标合法性校验（如移动速度检测），后续可扩展。
     */
    void OnMoveReq(uint32_t clientConnID, const char* data, uint16_t len);

    /** @brief 处理聊天请求：广播给地图内所有玩家 */
    void OnChatReq(uint32_t clientConnID, const char* data, uint16_t len);

    /** @brief 处理技能请求：委派 Lua 处理 */
    void OnSkillReq(uint32_t clientConnID, const char* data, uint16_t len);

    /**
     * @brief 处理 NPC 对话：callScriptBool → OnNpcTalk → guide.lua 等
     *
     * 成功时由 Lua send_npc_talk_rsp 下发 S2C_NPC_TALK_RSP；
     * 失败时 C++ 回错误码包。
     */
    void OnNpcTalkReq(uint32_t clientConnID, const char* data, uint16_t len);

    /** @brief 对话失败回包（code: 1=NPC无效 2=不同地图 3=脚本无响应） */
    void sendNpcTalkError(uint32_t clientConnID, uint64_t npcId, int32_t code);

    /** @brief 处理心跳请求：回包服务器时间 */
    void OnHeartbeatReq(uint32_t clientConnID, const char* data, uint16_t len);

    /**
     * @brief 向客户端发送消息（通过 GatewayServer 转发）
     *
     * 包格式：Msg_GW_SendToClient + body
     */
    void SendToClient(uint32_t clientConnID, uint8_t module, uint8_t sub,
                      const char* data, uint16_t len);

    void SendToClient(uint32_t clientConnID, uint16_t flatMsgId,
                      const char* data, uint16_t len);

    /**
     * @brief 广播消息给指定地图内的所有用户（可排除指定用户）
     *
     * 广播排除逻辑：
     * 1. 遍历所有在线用户 m_users；
     * 2. 跳过不在目标 mapID 中的用户（mapID==0 时广播所有地图）；
     * 3. 跳过 excludeUserID 指定的用户（通常为消息发起者，避免自己收到自己的广播）；
     * 4. 跳过 gatewayClientConn==0 的用户（连接无效，无法发送）；
     * 5. 对剩余用户逐一调用 SendToClient 转发消息。
     *
     * @param mapID          目标地图 ID，0 表示所有地图
     * @param excludeUserID  需要排除的用户 ID
     * @param msgID          消息协议号
     * @param data           消息体指针
     * @param len            消息体长度
     */
    void BroadcastToMap(uint32_t mapID, UserID excludeUserID,
                        uint16_t msgID, const char* data, uint16_t len);

    /** @brief Lua 回调：用户进入场景 */
    void CallLuaOnEnter(UserID userID, uint32_t mapID);

    /** @brief Lua 回调：用户离开场景 */
    void CallLuaOnLeave(UserID userID);

    /**
     * @brief Lua 回调：通用消息处理（OnMsg_XXXX）
     *
     * 根据 msgID 拼全局函数名；connID + 二进制 data 作为参数。
     */
    void CallLuaMsgHandler(uint32_t connID, uint8_t module, uint8_t sub,
                           const char* data, uint16_t len);

    /** @brief Lua 回调：技能请求 */
    void CallLuaSkillHandler(uint32_t connID, const char* data, uint16_t len);

    /** @brief 每帧 Tick（驱动用户/NPC loop + Lua OnTick） */
    void OnTick();

    /** @brief 为已加载地图创建默认 NPC（示例：新手引导官） */
    void initMapNpcs();

    /** @brief 场景启动成功：注册 AOI + SessionServer */
    void onSceneStarted(Scene& scene);

    /** @brief 场景关闭：注销 AOI + SessionServer */
    void onSceneStopped(Scene& scene);

    /** @brief 处理 SessionServer 的副本创建应答 */
    void OnCopyCreateRsp(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 处理 SessionServer 下发的副本创建指令 */
    void OnCopyCreateCmd(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 将 SceneEntry 填入客户端 SpawnEntity 协议 */
    static void fillSpawnFromEntry(const SceneEntry& entry, uint8_t entityType,
                                   Msg_S2C_SpawnEntity& spawn);

    /** @brief 实体进入 AOIServer 视野管理 */
    void sendAoiEnter(const SceneEntry& entry, uint8_t entityType);

    /** @brief 实体离开 AOIServer 视野管理 */
    void sendAoiLeave(EntryID entityId);

    /** @brief 向 SuperServer 注册 Scene 节点 */
    void RegisterToSuper();

    /** @brief 定时发送 Scene 心跳 */
    void SendHeartbeat();
    TcpServer  m_server;          /**< 入站监听（Gateway / Session） */
    TcpClient  m_superClient;     /**< 出站 SuperServer */
    TcpClient  m_sessionClient;   /**< 出站 SessionServer */
    TcpClient  m_recordClient;    /**< 出站 RecordServer */
    TcpClient  m_aoiClient;       /**< 出站 AOIServer */
    ConnID     m_gatewayInboundConn = INVALID_CONN_ID; /**< Gateway 入站连接（下行 GW_SEND_TO_CLIENT） */
    uint32_t   m_sceneID;         /**< 场景服务器编号 */
    uint32_t   m_hbSeq = 0;       /**< 心跳序列号 */
    uint16_t   m_listenPort = 9004;  /**< Scene 对内监听端口 */
    ServerEntry m_self;           /**< 本进程在 ServerList 中的拓扑条目（注册上报用） */
    ExternalServerHub m_externHub; /**< 外联 Logger / Global / Zone */
};
