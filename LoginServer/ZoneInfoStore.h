/**
 * @file    ZoneInfoStore.h
 * @brief   LoginServer 游戏区列表内存缓存与轮询选区
 *
 * 从 serverlist.xml 只读加载游戏区入口配置；登录下发网关时与
 * LoginGatewayRegistry 按 zone_id ↔ gatewayServerId 对齐。
 */

#pragma once

#include "../Common/ZoneCommon.h"
#include "../protocal/InternalMsg.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
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
    uint32_t maxOnline = 2000; /**< 负载阈值上限（计算畅通/繁忙/爆满） */
    bool enabled = false;      /**< 是否可登录 */
};

/**
 * @brief 游戏区运行时状态（Super 周期上报合并）
 */
struct ZoneRuntimeRow
{
    uint32_t onlineCount = 0;   /**< 在线人数 */
    uint32_t gatewayCount = 0;  /**< Super 上报的存活网关数 */
    bool alive = false;         /**< 区是否可达 */
    uint64_t lastReportMs = 0;  /**< 最近上报时间（ms） */
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
     * @brief 从 rpg_login.ZoneInfo 全量加载（保留供迁移/工具，LoginServer 启动不调用）
     * @param db 已连接 rpg_login 的 MySQL 句柄
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

    /**
     * @brief 合并 Super 上报的运行时区状态
     * @param report Super → Login 区状态包
     * @return 静态表中存在对应区时 true
     */
    bool applyZoneReport(const Msg_Login_ZoneStatusReport& report);

    /**
     * @brief 查询区服静态配置
     * @param gameType 游戏类型
     * @param zoneId   游戏区号
     * @param out      [out] 区服行
     * @return 存在时 true
     */
    bool findZone(uint8_t gameType, uint32_t zoneId, ZoneInfoRow& out) const;

    /**
     * @brief 获取运行时状态（无上报时返回 false）
     */
    bool getRuntime(uint8_t gameType, uint32_t zoneId, ZoneRuntimeRow& out) const;

    /**
     * @brief 计算客户端展示的负载等级
     * @param row           静态区配置
     * @param runtime       运行时状态（可为 nullptr）
     * @param gatewayCount  Login 侧存活网关数
     */
    static uint8_t computeLoadLevel(const ZoneInfoRow& row,
                                    const ZoneRuntimeRow* runtime,
                                    size_t gatewayCount);

    /**
     * @brief 合并注册表与 Super 上报网关数，计算区负载展示字段
     * @param row 静态区配置
     * @param store 区服缓存（查运行时）
     * @param registryGatewayCount Login 网关注册表中的网关数
     * @param outOnline 在线人数
     * @param outGatewayCount 有效网关数（≤255）
     * @param outLoadLevel ZoneLoadLevel
     */
    static void fillGatewayLoadFields(const ZoneInfoRow& row,
                                      const ZoneInfoStore& store,
                                      size_t registryGatewayCount,
                                      uint32_t& outOnline,
                                      uint8_t& outGatewayCount,
                                      uint8_t& outLoadLevel);

private:
    static uint64_t zoneKey(uint8_t gameType, uint32_t zoneId);

    /** @brief 重建 enabled 区服的轮询索引 */
    void rebuildEnabledOrder();

    std::vector<ZoneInfoRow> m_rows;       /**< 全表缓存 */
    std::vector<size_t> m_enabledIndices;  /**< enabled 行在 m_rows 中的下标 */
    size_t m_rrIndex = 0;                  /**< 轮询游标 */
    std::unordered_map<uint64_t, ZoneRuntimeRow> m_runtime; /**< 运行时覆盖 */
};
