/**
 * @file    GlobalServer.h
 * @brief  全局服务器 —— 全区数据管理（排行榜、全服公告等），可选启动
 *
 * ## 职责
 * - 排行榜维护（接收经 Super SS_EXTERN_FWD 转发的 GLB_RANK_UPDATE，排序保留前 100 名）
 * - 全区数据同步（GLB_DATA_SYNC 向已连接 inner 连接 fan-out；syncGlobalData 定时器尚未推送 rank）
 * - HTTP 入站 JSON API（/health、/rank；/getUserList 待 rpg_global 玩法接入）
 *
 * ## 连接
 * - 生产路径：SuperServer ExternalServerHub 出站连接 Global；Scene 等经 SS_EXTERN_FWD 访问
 * - 非 SceneServer 直连（历史注释已修正）
 *
 * ## 特性
 * - 可选服务（ENABLE_GLOBAL=1 或独立启动）
 * - 独立部署，配置见 GlobalServer/extern_global.xml
 * - 可选 MySQL 持久化（rpg_global，AllLittleThing 等全区表）
 */

#pragma once
#include "GlobalHttpApi.h"
#include "GlobalHttpClient.h"
#include "GlobalHttpServer.h"
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/UserBase.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/ExternServerConfig.h"
#include "../sdk/util/Singleton.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include "../sdk/http/HttpMessage.h"
#include <mysql/mysql.h>
#include <unordered_map>
#include <string>
#include <vector>

/**
 * @brief 排行榜条目
 */
struct RankEntry
{
    UserID   userID;       /**< 用户 ID */
    char     name[32];     /**< 用户名称 */
    uint32_t value;        /**< 排行数值（等级/战力/积分等） */
};

/**
 * @brief GlobalServer 核心类
 *
 * 单进程运行，不依赖 SuperServer（独立监听），各 SceneServer 直接连接。
 */
class GlobalServer : public INetCallback, public LazySingleton<GlobalServer>
{
public:
    friend class LazySingleton<GlobalServer>;
    friend void GlobalInternMsgRegister(GlobalServer& server);
    friend void GlobalGameZoneRankMsgRegister(GlobalServer& server);
    friend void GlobalGameZoneSyncMsgRegister(GlobalServer& server);
    /** @brief 获取 GlobalServer 单例指针 */
    static GlobalServer* Instance() { return &LazySingleton<GlobalServer>::Instance(); }

private:
    /** @brief 构造 GlobalServer（初始化榜单与连接表） */
    GlobalServer();

public:
    ~GlobalServer();

    /**
     * @brief 初始化 GlobalServer
     * @param cfg 外联配置（监听、日志、Database、Http）
     * @return 成功返回 true
     */
    bool Init(const ExternServerConfig& cfg);

    /** @brief 主循环（游戏 TCP + HTTP 入站/出站 + 定时器） */
    void Run();

    /** @brief SceneServer 等连接建立 */
    void OnConnect(ConnID id) override;

    /** @brief 连接断开时清理路由 */
    void OnDisconnect(ConnID id) override;

    /** @brief 处理排行榜更新、全服公告等协议 */
    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    /** @brief 注册 GlobalServer 的内部协议处理器 */
    void registerHandlers();

    /** @brief 可选 MySQL 连接（database.configured 时必选成功） */
    bool initDatabase(const DatabaseConfig& dbCfg);

    /**
     * @brief HTTP 入站请求入口（委托 GlobalHttpApi + HttpCodec）
     * @param connId 连接 ID
     * @param req    已解析请求
     * @return 完整 HTTP 响应字节流
     */
    std::string onHttpRequest(ConnID connId, const HttpRequest& req);

    void onRankUpdate(ConnID fromConn, const char* data, uint16_t len);

    void onDataSync(ConnID fromConn, const char* data, uint16_t len);

    void syncGlobalData();

    /** @brief 定时向 Http Client 对端发送 GET /health（仅 enabled 时） */
    void probeHttpPeer();

    TcpServer        m_server;      /**< 游戏协议 TCP（Scene 等连接） */
    GlobalHttpServer m_httpServer;  /**< HTTP 入站（9070 等） */
    GlobalHttpClient m_httpClient;  /**< HTTP 出站（extern enabled=1 时） */
    MYSQL*           m_db = nullptr; /**< rpg_global；全区杂项持久化 */
    std::vector<RankEntry>           m_rank;       /**< 内存排行榜，最多 100 条 */
    std::unordered_map<ConnID, bool> m_innerConns; /**< 游戏区内服连接 */
};
