/**
 * @file    LoginServer.h
 * @brief  外联登录服务器 —— 客户端账号校验与网关负载均衡
 *
 * ## 职责
 * - ClientListen：接收 C2S_ZONE_LIST_REQ / C2S_REGISTER_REQ / C2S_LOGIN_REQ
 * - 注册与登录均由 LoginServer 访问 GameUser 账号表
 * - RegisterListen：接收 Gateway LOGIN_GATEWAY_REGISTER / HEARTBEAT，维护网关表
 *
 * ## 部署
 * - 独立进程，读 LoginServer/extern_login.xml 与 serverlist.xml；不向 SuperServer 注册
 * - 游戏区 Gateway 经 loginserverlist.xml 连接 RegisterListen 口上报
 */

#pragma once

#include "LoginExternConfig.h"
#include "LoginGatewayRegistry.h"
#include "ZoneInfoStore.h"
#include "LoginAuthService.h"
#include "LoginRegisterService.h"
#include "LoginRechargeService.h"
#include "LoginGmService.h"
#include "../sdk/net/TcpServer.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/Singleton.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/log/Logger.h"
#include "../protocal/InternalMsg.h"

#include <mysql/mysql.h>

#include <cstdint>
#include <memory>
#include <string>

/**
 * @brief LoginServer 核心类（双 TcpServer：客户端 + 网关注册）
 */
class LoginServer : public LazySingleton<LoginServer>
{
public:
    friend class LazySingleton<LoginServer>;
    /** @brief 获取单例 */
    static LoginServer* Instance() { return &LazySingleton<LoginServer>::Instance(); }

private:
    LoginServer();

    /** @brief 析构（unique_ptr 桥接类型在 .cpp 中完整定义） */
    ~LoginServer();

public:
    /**
     * @brief 初始化双端口监听与可选 MySQL
     * @param cfg extern_login.xml 解析结果
     * @return 成功 true
     */
    bool Init(const LoginExternConfig& cfg);

    /** @brief 主循环 */
    void Run();

    TcpServer& clientServer() { return m_clientServer; }
    TcpServer& registerServer() { return m_registerServer; }
    LoginGatewayRegistry& gatewayRegistry() { return m_gatewayRegistry; }
    ZoneInfoStore& zoneInfoStore() { return m_zoneInfoStore; }
    MYSQL* db() { return m_db; }
    bool dbRequired() const { return m_dbRequired; }
    LoginAuthService& authService() { return m_authService; }
    LoginRegisterService& registerService() { return m_registerService; }
    LoginRechargeService& rechargeService() { return m_rechargeService; }
    LoginGmService& gmService() { return m_gmService; }

    /** @brief 客户端口：新连接 */
    void onClientConnect(ConnID id);

    /** @brief 客户端口：断开 */
    void onClientDisconnect(ConnID id);

    /** @brief 注册口：新连接 */
    void onRegisterConnect(ConnID id);

    /** @brief 注册口：断开 */
    void onRegisterDisconnect(ConnID id);

private:
    void registerHandlers();
    bool initDatabase(const DatabaseConfig& dbCfg);
    bool loadServerList(const std::string& path);
    void pruneGatewayTable();

    /** @brief 客户端口 INetCallback 桥接 */
    struct ClientPortBridge;

    /** @brief 注册口 INetCallback 桥接 */
    struct RegisterPortBridge;

    std::unique_ptr<ClientPortBridge> m_clientBridge;     /**< 客户端回调桥 */
    std::unique_ptr<RegisterPortBridge> m_registerBridge; /**< 注册口回调桥 */
    TcpServer m_clientServer;    /**< 玩家登录监听 */
    TcpServer m_registerServer;  /**< 网关注册监听 */
    LoginGatewayRegistry m_gatewayRegistry; /**< 存活网关表 */
    ZoneInfoStore        m_zoneInfoStore;     /**< serverlist.xml 区服缓存 */
    LoginAuthService     m_authService;     /**< 客户端登录 */
    LoginRegisterService m_registerService; /**< 客户端注册 */
    LoginRechargeService m_rechargeService; /**< 充值骨架 */
    LoginGmService       m_gmService;       /**< GM 骨架 */
    MYSQL* m_db = nullptr;       /**< 可选 MySQL（与 Record 同库） */
    bool m_dbRequired = false;   /**< 配置了 Database 则须连库成功 */
};
