/**
 * @file    ZoneServer.h
 * @brief  跨区服务器 —— 跨游戏区数据转发与逻辑处理，可选启动
 *
 * 独立部署，配置见 ZoneServer/extern_zone.xml；可选 MySQL。
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/ExternServerConfig.h"
#include "../sdk/util/Singleton.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <mysql/mysql.h>
#include <unordered_map>
#include <string>

/** @brief 区服唯一标识 */
using ZoneID = uint32_t;

/**
 * @brief 跨区路由条目
 */
struct ZoneRoute
{
    ZoneID  zoneID;  /**< 目标区服 ID */
    ConnID  connID;  /**< 该区服的内部连接 ID */
    bool    alive;   /**< 是否存活 */
};

/**
 * @brief ZoneServer 核心类
 */
class ZoneServer : public INetCallback, public LazySingleton<ZoneServer>
{
public:
    friend class LazySingleton<ZoneServer>;
    friend void ZoneInternMsgRegister(ZoneServer& server);
    static ZoneServer* Instance() { return &LazySingleton<ZoneServer>::Instance(); }

private:
    ZoneServer();

public:
    ~ZoneServer();

    /**
     * @brief 初始化 ZoneServer
     * @param cfg 外联配置
     * @return 成功返回 true
     */
    bool Init(const ExternServerConfig& cfg);

    void Run();

    void OnConnect(ConnID id) override;

    void OnDisconnect(ConnID id) override;

    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    void registerHandlers();

    bool initDatabase(const DatabaseConfig& dbCfg);

    void onCrossReq(ConnID fromConn, const char* data, uint16_t len);

    void onForward(ConnID fromConn, const char* data, uint16_t len);

    TcpServer m_server;
    MYSQL*    m_db = nullptr;
    std::unordered_map<ZoneID, ZoneRoute> m_routes;
};
