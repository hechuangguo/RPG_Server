/**
 * @file    AOIServer.h
 * @brief  视野管理服务器 —— 9 宫格 AOI 算法实现
 *
 * ## 职责
 * - 管理所有实体的空间位置
 * - 基于 9 宫格（3x3 邻域）算法计算视野范围
 * - 通知 SceneServer 实体进入/离开视野事件
 *
 * ## 9 宫格算法说明
 *
 * AOI（Area of Interest，兴趣区域）采用经典的 9 宫格网格划分方案：
 *
 * - 世界空间被划分为大小固定的正方形格子，每格边长为 GRID_SIZE（默认 200 游戏单位）。
 * - 每个实体的坐标通过 floorf(pos / GRID_SIZE) 映射到所在格子 (gx, gz)。
 * - Y 轴高度不参与 AOI 计算，仅使用 X-Z 平面的二维格子坐标。
 * - 每个实体的"可见区域"定义为当前格子及其 8 个相邻格子（共 9 格）。
 *   只有在这 9 格范围内的其他实体才会被通知为"进入视野"。
 *
 * ### 数据结构设计
 *
 * - m_entities: entityID → AOIEntity，存储每个实体的完整属性。
 * - m_entityGrid: entityID → Grid，快速查找实体当前所在格子。
 * - m_gridMap: 复合 Key → entityID 集合，用于高效获取某个格子内的所有实体。
 *   Key 编码方式：高 32 位 = mapID，低 32 位 = (gx << 16) | gz。
 *
 * ### 性能特点
 *
 * - 世界坐标 → 格子坐标：O(1) 数学运算。
 * - 获取邻域实体：最多遍历 9 个格子，每个格子的实体集合使用 unordered_set，查找 O(1)。
 * - 空间换时间：格子越多，每格实体越少，邻域查找越快，但内存占用略增。
 *
 * @code
 *    Grid 尺寸由 GRID_SIZE 控制（默认 200 游戏单位）
 *
 *    ┌───┬───┬───┐
 *    │(x-1,│(x, │(x+1,│
 *    │z-1) │z-1)│z-1) │
 *    ├───┼───┼───┤
 *    │(x-1,│(x, │(x+1,│   ← 中心格是实体所在格
 *    │ z ) │ z ) │ z ) │
 *    ├───┼───┼───┤
 *    │(x-1,│(x, │(x+1,│
 *    │z+1) │z+1) │z+1) │
 *    └───┴───┴───┘
 * @endcode
 *
 * ## 视野操作说明
 *
 * ### AOI_ENTER（进入视野）
 *
 * **触发时机**: 当一个新的实体（玩家或 NPC）首次出现在地图上时，
 * SceneServer 向 AOIServer 发送 AOI_ENTER_REQ 消息。
 *
 * **处理流程**:
 * 1. 解析请求中的 entityID、mapID、坐标等信息，构造 AOIEntity 存入 m_entities。
 * 2. 通过 WorldToGrid() 将世界坐标转换为格子坐标，更新 m_entityGrid 和 m_gridMap。
 * 3. 调用 NotifyViewChange()，向该实体所在 SceneServer 发送 AOI_VIEW_NOTIFY 消息，
 *    SceneServer 收到后会广播给周围玩家，表示"新实体出现在视野中"。
 *
 * ### AOI_LEAVE（离开视野）
 *
 * **触发时机**: 当实体从地图上移除（玩家下线、退出场景、NPC 销毁等）时，
 * SceneServer 向 AOIServer 发送 AOI_LEAVE_REQ 消息。
 *
 * **处理流程**:
 * 1. 先调用 NotifyViewChange()，通知 SceneServer 该实体已离开视野（enter=false），
 *    SceneServer 收到后会向周围玩家广播"实体消失"消息。
 * 2. 然后从 m_gridMap、m_entityGrid、m_entities 三个索引中彻底移除该实体的所有数据。
 *
 * ### AOI_MOVE（移动更新）
 *
 * **触发时机**: 当实体在地图上移动时，SceneServer 周期性或按距离阈值向 AOIServer 发送
 * AOI_MOVE_REQ 消息（通常由客户端的移动同步触发）。
 *
 * **处理流程**:
 * 1. 计算移动后的新格子坐标，与旧格子比较。
 * 2. **跨格移动**: 如果实体跨越了格子边界（oldG != newG），需要将实体从旧格子移除，
 *    加入新格子，并更新 m_entityGrid。SceneServer 也需要处理进入/离开视野的广播。
 * 3. **格内移动**: 如果实体仍在同一格子内，仅更新坐标，不触发视野变化事件。
 * 4. 通过 AOI_VIEW_NOTIFY 将移动信息转发给 SceneServer，由 SceneServer 广播给
 *    该实体视野范围内的其他玩家。
 *
 * ## 依赖关系
 * - 出站：SuperServer（注册）
 * - 入站：SceneServer（AOI_ENTER / AOI_LEAVE / AOI_MOVE）
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/UserBase.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/util/Singleton.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/util/ServerList.h"
#include "../sdk/util/ExternalServerHub.h"
#include "../sdk/util/LoginServerList.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>

/** @brief 9 宫格格子边长（世界坐标单位） */
constexpr float GRID_SIZE = 200.f;

/**
 * @brief AOI 系统中的实体
 */
struct AOIEntity
{
    uint64_t entityID;    /**< 实体 ID（userID 或 NPC ID） */
    uint32_t mapID;       /**< 所在地图 ID */
    float    x, y, z;     /**< 世界坐标 */
    bool     isPlayer;    /**< 是否玩家（true）或 NPC（false） */
    ConnID   sceneConnID; /**< 所在 SceneServer 的连接 ID */
};

/**
 * @brief 格子坐标（使用 gx/gz 两个维度，y 高度不计入 AOI）
 */
struct Grid
{
    int gx, gz;           /**< 格子索引 */
    bool operator==(const Grid& o) const { return gx==o.gx && gz==o.gz; }
};

/**
 * @brief Grid 的哈希函数（用于 unordered_map 的 Key）
 */
struct GridHash
{
    size_t operator()(const Grid& g) const
    { return std::hash<int64_t>()((int64_t)g.gx << 32 | (uint32_t)g.gz); }
};

/**
 * @brief AOIServer 核心类
 *
 * 单进程运行。提供实体空间索引：mapID + grid → entity set 的快速查找。
 */
class AOIServer : public INetCallback, public LazySingleton<AOIServer>
{
public:
    friend class LazySingleton<AOIServer>;
    /** @brief 获取 AOIServer 单例指针 */
    static AOIServer* Instance() { return &LazySingleton<AOIServer>::Instance(); }

private:
    /** @brief 构造 AOIServer（初始化 AOI 索引容器） */
    AOIServer();

public:

    /**
     * @brief 初始化 AOIServer
     * @param ip     监听 IP
     * @param port   监听端口（取自 ServerList 自身条目）
     * @param cfg    全局配置（提供 SuperServer 地址）
     * @param list   集群拓扑（用于解析对端地址与自身登记信息）
     * @param selfId 本进程实例编号
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg, const ServerList& list, uint32_t selfId);

    /** @brief 主循环：轮询网络并驱动 AOI 定时任务 */
    void Run();

    /** @brief 连接外联 Logger（loginserverlist.xml） */
    void setupExternalClients(const LoginServerList& list);

    void OnConnect(ConnID id) override;

    void OnDisconnect(ConnID id) override;

    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    /** @brief 注册 AOI 相关协议处理函数 */
    void RegisterHandlers();

    /**
     * @brief 世界坐标 → 格子坐标转换
     *
     * 使用 floorf 向下取整，确保坐标为负数时也能正确映射。
     * 例如：x=199.9 → gx=0, x=200.0 → gx=1, x=-1.0 → gx=-1
     *
     * @param x 世界 X 坐标
     * @param z 世界 Z 坐标（Y 不计入 AOI）
     * @return 所在格子索引
     */
    Grid WorldToGrid(float x, float z);

    /**
     * @brief 获取 3x3 邻域格子列表（含中心格共 9 个）
     *
     * 遍历中心格周围偏移量 dx ∈ [-1,1], dz ∈ [-1,1] 的所有组合，
     * 生成包含自身在内的 9 个相邻格子坐标。
     *
     * @param g 中心格子
     * @return 9 个邻域格子
     */
    std::vector<Grid> GetNeighborGrids(const Grid& g);

    /**
     * @brief 获取指定地图指定格子内的所有实体 ID
     * @param mapID 地图 ID
     * @param g     格子坐标
     * @return 实体 ID 列表
     */
    std::vector<uint64_t> GetEntitiesInGrid(uint32_t mapID, const Grid& g);

    /**
     * @brief 实体进入 AOI
     *
     * 记录实体信息，计算所在格子，通知视野变化。
     *
     * 触发时机：玩家登录进入场景、NPC 被动态生成等场景。
     * SceneServer 发送 AOI_ENTER_REQ，包含 entityID、mapID 和初始坐标。
     */
    void OnEnter(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 实体离开 AOI
     *
     * 先通知视野变化（让 SceneServer 广播"消失"），再清理所有索引数据。
     *
     * 触发时机：玩家下线、切换场景、NPC 被销毁等场景。
     * SceneServer 发送 AOI_LEAVE_REQ，包含 entityID。
     */
    void OnLeave(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 实体移动更新
     *
     * 检测是否跨越格子边界，如果是则更新格子索引。
     * 通过 AOI_VIEW_NOTIFY 通知 SceneServer 广播移动。
     *
     * 触发时机：玩家在场景中移动时，由 SceneServer 周期性上报坐标。
     * 移动频率通常由客户端的移动同步间隔控制（如每 200ms 一次）。
     */
    void OnMove(ConnID fromConn, const char* data, uint16_t len);

    /** @brief SceneServer 注册场景实例到 AOI */
    void OnSceneRegister(ConnID fromConn, const char* data, uint16_t len);

    /** @brief SceneServer 注销场景实例 */
    void OnSceneUnregister(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 通知 SceneServer 视野变化（实体进入/离开）
     * @param entityID 实体 ID
     * @param enter    true=进入视野, false=离开视野
     *
     * SceneServer 收到此通知后，会向该实体 9 宫格邻域内的所有玩家广播
     * "新实体出现"或"实体消失"消息，客户端据此创建/销毁实体表现。
     */
    void NotifyViewChange(uint64_t entityID, bool enter);

    /** @brief 向 SuperServer 注册 AOI 节点 */
    void RegisterToSuper();

    /** @brief 定时发送 AOI 存活心跳 */
    void SendHeartbeat();
    TcpServer  m_server;         /**< 入站监听（SceneServer） */
    TcpClient  m_superClient;    /**< 出站 SuperServer（注册、心跳） */
    uint32_t   m_hbSeq = 0;      /**< 心跳序列号 */
    ServerEntry m_self;          /**< 本进程在 ServerList 中的拓扑条目（注册上报用） */
    ExternalServerHub m_externHub; /**< 外联 Logger */
    /** @brief 实体索引：entityID → AOIEntity */
    std::unordered_map<uint64_t, AOIEntity>                    m_entities;
    /** @brief 已注册场景：sceneInstanceId → Msg_AOI_SceneRegister */
    std::unordered_map<uint64_t, Msg_AOI_SceneRegister>        m_scenes;
    /** @brief 实体所在格子：entityID → Grid */
    std::unordered_map<uint64_t, Grid>                         m_entityGrid;
    /**
     * @brief 格子空间索引
     *
     * Key: 高 32 位 = mapID, 低 32 位 = (gx << 16) | gz
     * Value: 该格子内的 entityID 集合
     *
     * 设计意图：将三维空间（mapID + gx + gz）编码为一个 uint64_t 作为哈希表的 key，
     * 避免嵌套 map 的开销，实现 O(1) 的格子查找。
     */
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> m_gridMap;
};
