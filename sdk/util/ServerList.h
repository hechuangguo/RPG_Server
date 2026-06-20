/**
 * @file    ServerList.h
 * @brief  集群拓扑表 ServerList —— 内存容器与启动期拉取客户端
 *
 * 设计：
 * - DB 表 ServerList 由 SuperServer 启动只读加载；子服务器（Session/Record/AOI/
 *   Scene/Gateway）不直连 DB，启动时经 SuperServer 拉取拓扑。
 * - 本文件提供：
 *   1) ServerEntry / ServerList —— 拓扑条目与按类型索引的容器（声明在 .h，实现在 .cpp）。
 *   2) ServerListClient::fetch —— 子服启动期用临时连接向 SuperServer 同步拉取 ServerList。
 *
 * 依赖说明：包含 protocal/InternalMsg.h 以复用 SubServerType 与 ServerList 协议结构。
 */

#pragma once

#include "../../protocal/InternalMsg.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief 集群中单个服务器进程的拓扑信息（对应 DB 表 ServerList 一行）
 */
struct ServerEntry
{
    uint32_t      id   = 0;                       /**< 服务器实例编号 */
    SubServerType type = SubServerType::UNKNOWN;  /**< 服务器类型 */
    std::string   ip;                             /**< 监听 IP */
    uint16_t      port = 0;                        /**< 监听端口 */
    std::string   name;                            /**< 服务器名 */
};

/**
 * @brief ServerList 内存容器（拓扑条目集合，支持按类型/编号查找）
 */
class ServerList
{
public:
    /** @brief 追加一个拓扑条目 */
    void add(const ServerEntry& entry);

    /** @brief 清空所有条目 */
    void clear();

    /** @brief 条目数量 */
    size_t size() const;

    /** @brief 只读访问全部条目 */
    const std::vector<ServerEntry>& all() const;

    /**
     * @brief 精确查找指定类型与编号的条目
     * @param type 服务器类型
     * @param id   实例编号
     * @return 命中返回条目指针；未命中返回 nullptr
     */
    const ServerEntry* find(SubServerType type, uint32_t id) const;

    /**
     * @brief 查找指定类型的首个条目（单实例部署的常用入口）
     * @param type 服务器类型
     * @return 命中返回条目指针；未命中返回 nullptr
     */
    const ServerEntry* findFirst(SubServerType type) const;

    /**
     * @brief 收集指定类型的全部条目（多实例水平扩展）
     * @param type 服务器类型
     * @param out  输出指针列表（指向内部存储，生命周期同本对象）
     */
    void findAll(SubServerType type, std::vector<const ServerEntry*>& out) const;

private:
    std::vector<ServerEntry> m_entries;  /**< 拓扑条目列表 */
};

/**
 * @brief 启动期 ServerList 拉取客户端（子服务器使用）
 */
class ServerListClient
{
public:
    /**
     * @brief 向 SuperServer 同步拉取 ServerList（阻塞至成功或超时）
     *
     * 内部建立到 SuperServer 的临时连接，发送 S2S_SERVERLIST_REQ，
     * 轮询等待 S2S_SERVERLIST_RSP 并解析为 out。拉取完成后关闭临时连接，
     * 调用方随后再以业务连接连接 SuperServer 并注册。
     *
     * @param superIP    SuperServer 监听 IP
     * @param superPort  SuperServer 监听端口
     * @param selfType   请求方服务器类型
     * @param selfID     请求方服务器实例编号
     * @param out        [out] 解析得到的 ServerList
     * @param timeoutMs  TLS 就绪与 RPC 响应各自的上限毫秒数（默认 10000，两阶段独立计时）
     * @return 成功返回 true；连接失败或超时返回 false
     */
    static bool fetch(const std::string& superIP, uint16_t superPort,
                      SubServerType selfType, uint32_t selfID,
                      ServerList& out, int timeoutMs = 10000);
};
