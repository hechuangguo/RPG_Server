/**
 * @file    SessionServer.h
 * @brief  会话服务器 —— 社会关系 + 全区场景/副本管理
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include "../common/ClientMsg.h"
#include "../sdk/net/MsgId.h"
#include "SessionUser.h"
#include "SessionUserManager.h"
#include "SessionSceneManager.h"
#include <mysql/mysql.h>
#include <string>

class SessionServer : public INetCallback
{
public:
    /** @brief 构造 SessionServer，准备网络与状态容器 */
    SessionServer();
    /** @brief 析构 SessionServer，释放 DB 连接等资源 */
    ~SessionServer();

    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg);

    /**
     * @brief 启动主循环
     *
     * 轮询网络事件并驱动定时任务（心跳、自动存档）。
     */
    void Run();

    void OnConnect(ConnID id) override;

    void OnDisconnect(ConnID id) override;

    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    /** @brief 初始化 MySQL 连接（Session 专用存档） */
    bool InitDB(const ServerConfig& cfg);

    /** @brief 注册 Session 服间消息处理器 */
    void RegisterHandlers();

    /**
     * @brief Gateway 转发的客户端消息（社交/任务等）
     */
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

    /** @brief 自动保存在线用户的 Session 数据 */
    void AutoSaveAll();

    TcpServer             m_server;       /**< Session 对内监听 */
    TcpClient             m_superClient;  /**< 到 SuperServer 的控制连接 */
    MYSQL*                m_db;           /**< Session 服 DB 句柄 */
    uint32_t              m_hbSeq = 0;    /**< 心跳序列号 */
    SessionUserManager    m_userManager;  /**< 在线用户与离线消息管理 */
    SessionSceneManager   m_sceneManager; /**< 全区场景/副本注册表 */
};
