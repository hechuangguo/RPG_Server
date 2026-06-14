/**
 * @file    ZoneInfoStore.h
 * @brief   LoginServer 游戏区列表内存缓存与轮询选区
 *
 * 从 serverlist.xml 只读加载游戏区入口配置；登录下发网关时与
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
 * @brief 游戏区列表：加载、轮询选取 enabled 区服
 */
class ZoneInfoStore
{
public:
    /**
     * @brief 从 serverlist.xml 全量加载游戏区列表
     * @param path XML 文件路径
     * @return 成功 true
     */
    bool loadFromFile(const char* path);

    /**
     * @brief 从 MySQL 全量加载 ZoneInfo（保留供迁移/工具，LoginServer 启动不调用）
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

    /**
     * @brief 列出全部或按 gameType 过滤的区服（含 disabled）
     * @param out            [out] 输出列表
     * @param gameTypeFilter 0xFF=全部，否则仅匹配该 gameType
     */
    void listAll(std::vector<ZoneInfoRow>& out, uint8_t gameTypeFilter) const;

private:
    /** @brief 重建 enabled 区服的轮询索引 */
    void rebuildEnabledOrder();

    std::vector<ZoneInfoRow> m_rows;       /**< 全表缓存 */
    std::vector<size_t> m_enabledIndices;  /**< enabled 行在 m_rows 中的下标 */
    size_t m_rrIndex = 0;                  /**< 轮询游标 */
};
