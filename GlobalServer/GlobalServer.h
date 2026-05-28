/**
 * @file    GlobalServer.h
 * @brief  全局服务器 —— 全区数据管理（排行榜、全服公告等），可选启动
 *
 * ## 职责
 * - 排行榜维护（接收各 SceneServer 的排行更新，排序保留前 100 名）
 * - 全区数据同步（向所有连接的 SceneServer 广播全局数据）
 * - 全服公告推送
 *
 * ## 特性
 * - 可选服务（通过环境变量 ENABLE_GLOBAL=1 控制是否启动）
 * - 所有游戏区共享一个 GlobalServer 进程
 * - 不依赖其他服务器，独立监听
 *
 * ## 使用场景
 * - 全服排行榜查询
 * - 全服活动数据同步
 * - 跨区公告推送
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/UserBase.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <string>
#include <vector>

/**
 * @brief 排行榜条目
 */
struct RankEntry
{
    UserID   userID;       /**< 用户 ID */
    char     name[32];     /**< 用户名称 */
    uint32_t value;        /**< 排行数值（等级/战力/积分等） */
};

/**
 * @brief GlobalServer 核心类
 *
 * 单进程运行，不依赖 SuperServer（独立监听），各 SceneServer 直接连接。
 */
class GlobalServer : public INetCallback
{
public:
    /** @brief 构造 GlobalServer（初始化榜单与连接表） */
    GlobalServer();

    /**
     * @brief 初始化 GlobalServer
     * @param ip   监听 IP
     * @param port 监听端口
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port);

    /** @brief 主循环 */
    void Run();

    /** @brief SceneServer 等连接建立 */
    void OnConnect(ConnID id) override;

    /** @brief 连接断开时清理路由 */
    void OnDisconnect(ConnID id) override;

    /** @brief 处理排行榜更新、全服公告等协议 */
    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    /** @brief 注册 GlobalServer 的内部协议处理器 */
    void RegisterHandlers();

    /**
     * @brief 处理排行榜更新
     *
     * ## 排行榜更新逻辑
     *
     * 当 SceneServer 中的用户属性发生变化（如升级、战力提升、积分增加等）时，
     * SceneServer 会向 GlobalServer 发送 GLB_RANK_UPDATE 消息，携带一个 RankEntry 条目。
     *
     * 处理步骤：
     * 1. **追加新条目**: 将收到的 RankEntry 直接追加到 m_rank 向量末尾。
     *    注意：这里不检查该用户是否已在排行榜中（允许存在同一用户的多个历史快照），
     *    如果需要去重，应在排序后遍历移除重复 userID 的旧条目。
     * 2. **降序排序**: 按 value（排行数值）从高到低排序，value 越大排名越靠前。
     * 3. **截断保留**: 只保留前 100 名，超出部分通过 resize 截断释放。
     *
     * ## 性能说明
     *
     * 每次更新都进行全量排序，时间复杂度 O(N log N)，其中 N ≤ 101（最多 100+1 条）。
     * 由于排行榜条目数量上限固定为 100，实际开销可忽略不计。
     * 如果排行榜容量大幅扩展（如 10000+），应考虑改用堆（priority_queue）或
     * 有序容器（如 std::set）来优化插入性能。
     *
     * ## 扩展方向
     *
     * - 支持多维度排行（等级榜、战力榜、积分榜）：可通过 RankEntry 增加 rankType 字段，
     *   将 m_rank 拆分为多个向量分别维护。
     * - 排行榜持久化：定时将 m_rank 序列化写入数据库或文件，重启后可恢复。
     */
    void OnRankUpdate(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 处理数据同步请求
     *
     * 广播给所有已连接的 SceneServer。
     */
    void OnDataSync(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 定时同步全区数据（广播排行榜等）
     *
     * 每 60 秒执行一次，将当前排行榜快照广播给所有已连接的 SceneServer。
     * 各 SceneServer 收到后更新本地缓存，用于客户端排行榜查询。
     *
     * TODO: 序列化排行榜数据（将 m_rank 转为字节流），广播给各 SceneServer。
     *       建议协议格式：[count:2][userID:8][name:32][value:4] × count
     */
    void SyncGlobalData();
    TcpServer m_server;                        /**< 监听内部连接 */
    std::vector<RankEntry>              m_rank;             /**< 排行榜（已排序，最多 100 条） */
    std::unordered_map<ConnID, bool>    m_innerConns;       /**< 内部连接记录：connID → alive */
};
