/**
 * @file    ZoneInfoStore.h
 * @brief   LoginServer ZoneInfo 表内存缓存与轮询选区
 *
 * 从 MySQL ZoneInfo 表只读加载游戏区入口配置；登录下发网关时与
 * LoginGatewayRegistry 按 zone_id ↔ gatewayServerId 对齐。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <mysql/mysql.h>

/**
 * @brief ZoneInfo 表单行（内存表示）
 */
struct ZoneInfoRow
{
    uint32_t zoneId = 0;       /**< 游戏区号 */
    uint8_t gameType = 0;      /**< 游戏类型（0=当前 RPG） */
    std::string name;          /**< 区服显示名 */
    std::string ip;            /**< 入口 IP（VIP 或对外地址） */
    uint16_t superPort = 0;    /**< SuperServer 端口（元数据） */
    bool enabled = false;      /**< 是否可登录 */
};

/**
 * @brief ZoneInfo 表：加载、轮询选取 enabled 区服
 */
class ZoneInfoStore
{
public:
    /**
     * @brief 从 MySQL 全量加载 ZoneInfo
     * @param db 已连接的 MySQL 句柄
     * @return 成功 true
     */
    bool loadFromDb(MYSQL* db);

    /**
     * @brief 轮询选取一条 enabled 区服记录
     * @param out [out] 选中的区服
     * @return 有可选取区服时 true
     */
    bool pickRoundRobin(ZoneInfoRow& out);

    /**
     * @brief 查询指定区服是否 enabled
     * @param gameType 游戏类型
     * @param zoneId   游戏区号
     * @return enabled=1 时 true
     */
    bool isZoneEnabled(uint8_t gameType, uint32_t zoneId) const;

    /** @brief 已加载行数（含 disabled） */
    size_t size() const { return m_rows.size(); }

private:
    /** @brief 重建 enabled 区服的轮询索引 */
    void rebuildEnabledOrder();

    std::vector<ZoneInfoRow> m_rows;       /**< 全表缓存 */
    std::vector<size_t> m_enabledIndices;  /**< enabled 行在 m_rows 中的下标 */
    size_t m_rrIndex = 0;                  /**< 轮询游标 */
};
