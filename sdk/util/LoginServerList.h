/**
 * @file    LoginServerList.h
 * @brief   外联服客户端配置 —— loginserverlist.xml 解析与容器
 *
 * 游戏区进程读取项目根 loginserverlist.xml，获取 Logger/Global/Zone
 * 远端 ip/port/reconnect，用于 TcpClient 出站连接；外联服自身监听配置见 extern_*.xml。
 */

#pragma once

#include "../../protocal/InternalMsg.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 单个外联服连接配置（对应 loginserverlist.xml 中一个节点）
 */
struct ExternalServerEntry
{
    SubServerType type     = SubServerType::UNKNOWN; /**< 外联服类型（LOGGER/GLOBAL/ZONE） */
    std::string   ip;                                /**< 远端 IP */
    uint16_t      port     = 0;                       /**< 远端端口；0 表示不连接 */
    bool          reconnect = false;                  /**< 断线后是否自动重连 */
};

/**
 * @brief 外联服配置列表容器
 */
class LoginServerList
{
public:
    /** @brief 追加一条外联配置 */
    void add(const ExternalServerEntry& entry);

    /** @brief 清空 */
    void clear();

    /** @brief 条目数 */
    size_t size() const;

    /** @brief 只读访问全部条目 */
    const std::vector<ExternalServerEntry>& all() const;

    /**
     * @brief 按类型查找外联配置
     * @param type LOGGER / GLOBAL / ZONE
     * @return 命中且 port>0 时返回指针；否则 nullptr
     */
    const ExternalServerEntry* find(SubServerType type) const;

private:
    std::vector<ExternalServerEntry> m_entries; /**< 外联条目列表 */
};

/**
 * @brief loginserverlist.xml 加载器
 */
class LoginServerListLoader
{
public:
    /**
     * @brief 从 XML 加载外联服列表
     * @param path   loginserverlist.xml 路径
     * @param out    [out] 解析结果
     * @param errOut 可选错误信息
     * @return 解析成功返回 true（文件不存在或全空节点亦视为成功，由调用方检查 size）
     */
    static bool Load(const char* path, LoginServerList& out, std::string* errOut = nullptr);
};
