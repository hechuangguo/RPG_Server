/**
 * @file    LoginGatewayRegistry.h
 * @brief   LoginServer 内存网关表与轮询负载均衡
 */

#pragma once

#include "../sdk/timer/TimerMgr.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief 单条已注册网关记录
 */
struct LoginGatewayEntry
{
    uint32_t gatewayServerId = 0; /**< 网关实例 ID */
    uint32_t zoneId = 0;          /**< 所属游戏区号 */
    uint8_t gameType = 0;         /**< 游戏类型 */
    std::string ip;              /**< 客户端可连 IP */
    uint16_t port = 0;           /**< 客户端端口 */
    std::string name;            /**< 网关名称 */
    std::string zoneName;        /**< 区服名 */
    uint64_t lastHeartbeatMs = 0; /**< 最近心跳/注册时间（ms） */
};

/**
 * @brief 网关表：注册、心跳刷新、超时剔除、轮询选取
 */
class LoginGatewayRegistry
{
public:
    /**
     * @brief 注册或刷新网关
     * @param entry 网关信息（gatewayServerId 为键）
     */
    void upsert(const LoginGatewayEntry& entry);

    /**
     * @brief 刷新网关心跳时间
     * @param gatewayServerId 网关 ID
     * @return 存在则 true
     */
    bool touch(uint32_t gatewayServerId);

    /**
     * @brief 剔除超时未心跳的网关
     * @param nowMs 当前时间戳
     * @param timeoutMs 超时阈值（默认 30s）
     */
    void pruneStale(uint64_t nowMs, uint64_t timeoutMs = 30000);

    /**
     * @brief 轮询选取一条存活网关（无可用时返回 false）
     * @param out [out] 选中的网关
     */
    bool pickRoundRobin(LoginGatewayEntry& out);

    /**
     * @brief 按 gatewayServerId 选取网关（与 ZoneInfo.zone_id 对齐）
     * @param gatewayServerId 网关实例 ID
     * @param out             [out] 选中的网关
     * @return 存在且存活时 true
     */
    bool pickByServerId(uint32_t gatewayServerId, LoginGatewayEntry& out);

    /**
     * @brief 在指定游戏区内轮询选取网关
     * @param zoneId   游戏区号
     * @param gameType 游戏类型
     * @param out      [out] 选中的网关
     */
    bool pickByZone(uint32_t zoneId, uint8_t gameType, LoginGatewayEntry& out);

    /**
     * @brief 统计指定区存活网关数
     */
    size_t countForZone(uint32_t zoneId, uint8_t gameType) const;

    /** @brief 当前存活网关数量 */
    size_t size() const { return m_entries.size(); }

private:
    void rebuildOrder();

    std::unordered_map<uint32_t, LoginGatewayEntry> m_entries; /**< gatewayId → 记录 */
    std::vector<uint32_t> m_order;                             /**< 轮询顺序 */
    std::unordered_map<uint64_t, size_t> m_zoneRrIndex;         /**< 区内轮询游标 */
    size_t m_rrIndex = 0;                                      /**< 全局轮询游标 */
};
