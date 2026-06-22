/**
 * @file    ExternalServerHub.h
 * @brief   游戏区进程外联服连接聚合（Logger / Global / Zone / Login）
 */

#pragma once

#include "ExternalServerConnector.h"
#include "LoginServerList.h"

#include <cstdint>

/**
 * @brief 按进程需求管理多条外联出站连接
 */
class ExternalServerHub
{
public:
    /** @brief 构造 Hub；Login 连接器使用 DispatchingNetCallback 派发注册响应 */
    ExternalServerHub()
        : m_login(&m_loginMsgCb)
    {}

    /**
     * @brief 根据 loginserverlist 与进程需求装配连接器
     * @param list        已加载的外联配置
     * @param wantLogger  是否连接 LoggerServer
     * @param wantGlobal  是否连接 GlobalServer
     * @param wantZone    是否连接 ZoneServer
     * @param wantLogin   是否连接 LoginServer（网关注册口）
     */
    void configure(const LoginServerList& list, bool wantLogger,
                   bool wantGlobal, bool wantZone, bool wantLogin = false);

    /** @brief 对已装配且 port>0 的条目发起连接 */
    void connectAll();

    /** @brief 轮询所有外联连接 */
    void poll();

    /**
     * @brief 所有外联连接的重连节拍
     * @param nowMs 当前毫秒时间戳
     * @param skipType 跳过重连的连接器类型（如票据校验在途时暂缓 Login）
     */
    void tickReconnect(uint64_t nowMs, SubServerType skipType = SubServerType::UNKNOWN);

    /**
     * @brief 获取指定类型的连接器
     * @param type LOGGER / GLOBAL / ZONE / LOGIN
     * @return 对应连接器；未装配时返回 nullptr
     */
    ExternalServerConnector* connector(SubServerType type);

    /** @brief 获取指定类型的 TcpClient（未装配或未配置时 nullptr） */
    TcpClient* client(SubServerType type);

private:
    ExternalServerConnector m_logger; /**< 到 LoggerServer */
    ExternalServerConnector m_global; /**< 到 GlobalServer */
    ExternalServerConnector m_zone;   /**< 到 ZoneServer */
    DispatchingNetCallback m_loginMsgCb; /**< Login 注册口入站派发 */
    ExternalServerConnector m_login;  /**< 到 LoginServer（网关注册） */
    bool m_wantLogger = false;      /**< 本进程是否需要 Logger 外联 */
    bool m_wantGlobal = false;      /**< 本进程是否需要 Global 外联 */
    bool m_wantZone   = false;      /**< 本进程是否需要 Zone 外联 */
    bool m_wantLogin  = false;      /**< 本进程是否需要 Login 外联 */
};
