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
#include "../sdk/util/RoleBase.h"
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
    RoleID   roleID;       /**< 角色 ID */
    char     name[32];     /**< 角色名称 */
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
    GlobalServer() : m_server(this) {}

    /**
     * @brief 初始化 GlobalServer
     * @param ip   监听 IP
     * @param port 监听端口
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port)
    {
        Logger::Instance().SetServerName("GlobalServer");
        if (!m_server.Start(ip, port)) { LOG_FATAL("GlobalServer start failed"); return false; }
        RegisterHandlers();
        // 每 60 秒同步一次全区数据
        TimerMgr::Instance().Register(60000, 60000, [this]{ SyncGlobalData(); });
        LOG_INFO("GlobalServer started on %s:%d", ip.c_str(), port);
        return true;
    }

    /** @brief 主循环 */
    void Run()
    {
        while (true) { m_server.Poll(10); TimerMgr::Instance().Update(); }
    }

    void OnConnect(ConnID id)    override { LOG_DEBUG("GlobalServer conn=%u", id); }
    void OnDisconnect(ConnID id) override { LOG_WARN("GlobalServer conn=%u lost", id); }
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::GLB_RANK_UPDATE,
            [this](uint32_t c, const char* d, uint16_t l){ OnRankUpdate(c, d, l); });
        d.Register((uint16_t)InternalMsgID::GLB_DATA_SYNC,
            [this](uint32_t c, const char* d, uint16_t l){ OnDataSync(c, d, l); });
    }

    /**
     * @brief 处理排行榜更新
     *
     * 追加新条目 → 按 value 降序排序 → 截断到前 100 名。
     */
    void OnRankUpdate(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(RankEntry)) return;
        const auto* entry = reinterpret_cast<const RankEntry*>(data);
        m_rank.push_back(*entry);
        std::sort(m_rank.begin(), m_rank.end(),
                  [](const RankEntry& a, const RankEntry& b){ return a.value > b.value; });
        if (m_rank.size() > 100) m_rank.resize(100);
    }

    /**
     * @brief 处理数据同步请求
     *
     * 广播给所有已连接的 SceneServer。
     */
    void OnDataSync(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        LOG_DEBUG("GlobalDataSync len=%d", len);
        for (auto& [cid, _] : m_innerConns)
            m_server.SendMsg(cid, (uint16_t)InternalMsgID::GLB_DATA_SYNC, data, len);
    }

    /** @brief 定时同步全区数据（广播排行榜等） */
    void SyncGlobalData()
    {
        LOG_INFO("GlobalServer: syncing rank to all scene servers. rank size=%zu", m_rank.size());
        // TODO: 序列化排行榜数据广播给各 SceneServer
    }

    TcpServer m_server;                        /**< 监听内部连接 */
    std::vector<RankEntry>              m_rank;             /**< 排行榜（已排序） */
    std::unordered_map<ConnID, bool>    m_innerConns;       /**< 内部连接记录：connID → alive */
};
