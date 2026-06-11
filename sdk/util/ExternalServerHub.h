/**
 * @file    ExternalServerHub.h
 * @brief   游戏区进程外联服连接聚合（Logger / Global / Zone）
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
    /**
     * @brief 根据 loginserverlist 与进程需求装配连接器
     * @param list        已加载的外联配置
     * @param wantLogger  是否连接 LoggerServer
     * @param wantGlobal  是否连接 GlobalServer
     * @param wantZone    是否连接 ZoneServer
     */
    void configure(const LoginServerList& list, bool wantLogger,
                   bool wantGlobal, bool wantZone);

    /** @brief 对已装配且 port>0 的条目发起连接 */
    void connectAll();

    /** @brief 轮询所有外联连接 */
    void poll();

    /** @brief 所有外联连接的重连节拍 */
    void tickReconnect();

    /**
     * @brief 获取指定类型的连接器
     * @param type LOGGER / GLOBAL / ZONE
     * @return 对应连接器；未装配时返回 nullptr
     */
    ExternalServerConnector* connector(SubServerType type);

    /** @brief 获取指定类型的 TcpClient（未装配或未配置时 nullptr） */
    TcpClient* client(SubServerType type);

private:
    ExternalServerConnector m_logger; /**< 到 LoggerServer */
    ExternalServerConnector m_global; /**< 到 GlobalServer */
    ExternalServerConnector m_zone;   /**< 到 ZoneServer */
    bool m_wantLogger = false;      /**< 本进程是否需要 Logger 外联 */
    bool m_wantGlobal = false;      /**< 本进程是否需要 Global 外联 */
    bool m_wantZone   = false;      /**< 本进程是否需要 Zone 外联 */
};
