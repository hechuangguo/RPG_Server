/**
 * @file    SessionServer.h
 * @brief  会话服务器 —— 社会关系 + 全区场景/副本管理
 *
 * ## 依赖关系
 * - 出站：SuperServer / RecordServer（Relation）/ 外联 Zone 等
 * - 入站：GatewayServer / SceneServer
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/util/ServerList.h"
#include "../sdk/util/GameZoneExternSender.h"
#include "../sdk/util/RelationWireUtil.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include "../Common/ClientMsg.h"
#include "../sdk/net/MsgId.h"
#include "SessionUser.h"
#include <string>

/**
 * @brief 会话服务器 —— 社会关系 + 全区场景/副本管理
 */
class SessionServer : public INetCallback
{
public:
    /** @brief 构造 SessionServer，准备网络与状态容器 */
    SessionServer();

    /** @brief 析构 SessionServer */
    ~SessionServer();

    /**
     * @brief 初始化网络、Record 预载与消息处理器
     * @param ip     监听 IP
     * @param port   监听端口（取自 ServerList 自身条目）
     * @param cfg    服务器配置（含 SuperServer 地址）
     * @param list   集群拓扑（用于自身登记信息）
     * @param selfId 本进程实例编号
     */
    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg, const ServerList& list, uint32_t selfId);

    /** @brief 主循环所在进程内的 SessionServer 实例（main 栈对象） */
    static SessionServer* active() { return s_active; }

    void Run();

    /** @brief 经 Super 转发到独立外联服 */
    GameZoneExternSender& externSender() { return m_externSender; }

    void SendToClient(uint32_t clientConnID, uint8_t module, uint8_t sub,
                      const char* data, uint16_t len);

    void SendToClient(uint32_t clientConnID, uint16_t flatMsgId,
                      const char* data, uint16_t len);

    /** @brief 内部连接建立（SceneServer 等） */
    void OnConnect(ConnID id) override;

    /** @brief 内部连接断开，清理场景绑定 */
    void OnDisconnect(ConnID id) override;

    /** @brief 收到服间消息后派发给 MsgDispatcher */
    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

    /**
     * @brief 同步从 Record 加载单用户 Relation（启动期/OnLoadUserReq）
     * @param userID 用户 ID
     * @param out    输出行
     * @return 成功 true
     */
    bool loadRelationSync(UserID userID, RelationRowData& out);

    /**
     * @brief 向 Record 发送 Relation 保存（异步，不等待响应）
     * @param row 待保存行
     * @return 发送成功 true
     */
    bool saveRelation(const RelationRowData& row);

    /** @brief Init 阶段短循环 poll（仅启动预载/同步加载用） */
    void pollForRelationSync();

private:
    /** @brief 注册 Session 服间消息处理器 */
    void RegisterHandlers();

    /**
     * @brief Gateway 转发的客户端消息（社交/任务等）
     */
    /** @brief Gateway 入站连接（下行 GW_SEND_TO_CLIENT） */
    void OnGatewayClientMsg(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 处理社交类客户端协议（好友/黑名单等） */
    void handleSocialClientMsg(uint32_t clientConnId, uint8_t sub,
                               const char* data, uint16_t len);

    /** @brief 处理任务类客户端协议（任务查询/更新） */
    void handleQuestClientMsg(uint32_t clientConnId, uint8_t sub,
                              const char* data, uint16_t len);

    /** @brief 向 SuperServer 注册本 Session 节点 */
    void RegisterToSuper();

    /** @brief 定时向 SuperServer 发送心跳 */
    void SendHeartbeat();

    /** @brief 启动期经 Record 预载 Relation */
    bool preloadRelations();

    /** @brief Record 预载响应 */
    void OnRelationPreloadRsp(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Record 单用户加载响应 */
    void OnRelationLoadRsp(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 处理加载用户请求（供登录/切图流程读取 Session 数据） */
    void OnLoadUserReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 处理保存用户请求（落 Session 社交数据） */
    void OnSaveUserReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 处理好友关系变更广播（跨服同步） */
    void OnFriendUpdate(ConnID fromConn, const char* data, uint16_t len);

    /** @brief SceneServer 注册普通/副本场景 */
    void OnSceneRegisterReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief SceneServer 注销场景实例（普通图或副本） */
    void OnSceneUnregister(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 副本创建：复用已有或负载均衡分配新副本 */
    void OnCopyCreateReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Super 按 mapId 解析 sceneServerId */
    void OnResolveMapReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 自动保存在线用户的 Session 数据 */
    void AutoSaveAll();

    TcpServer             m_server;       /**< 入站：Gateway / Scene */
    TcpClient             m_superClient;  /**< 出站 SuperServer */
    TcpClient             m_recordClient; /**< 出站 RecordServer（Relation） */
    uint32_t              m_hbSeq = 0;    /**< 心跳序列号 */
    ServerEntry           m_self;         /**< 本进程在 ServerList 中的拓扑条目（注册上报用） */
    GameZoneExternSender  m_externSender; /**< 经 Super 转发外联服 */
    ConnID                m_gatewayInboundConn = INVALID_CONN_ID;
    bool                  m_relationPreloadDone = false; /**< 启动预载是否完成 */
    bool                  m_relationPreloadOk   = false; /**< 启动预载是否成功 */
    bool                  m_relationLoadDone = false;    /**< 同步单用户加载完成 */
    bool                  m_relationLoadOk   = false;    /**< 同步单用户加载成功 */
    RelationRowData       m_relationLoadRow;             /**< 同步加载结果缓存 */

    static SessionServer* s_active;
};
