/**
 * @file    RecordServer.h
 * @brief  数据库服务器 —— 用户数据持久化（MySQL）、账号登录验证
 *
 * ## 职责
 * - MySQL 直连：账号验证、用户数据加载/保存
 * - 定时自动存档（每 60 秒 autoSaveAll）
 * - 内部通信：出站 SuperServer；入站 Gateway / Scene / Session
 *
 * ## 依赖关系
 * - 出站：SuperServer（注册）
 * - 入站：GatewayServer（登录验证）、SceneServer（存档）、SessionServer（Relation）
 * - 被 SuperServer（加载用户）经入站连接调用
 *
 * ## 数据流
 * @code
 *   GatewayServer ──(REC_VALIDATE_TOKEN_REQ)──→ RecordServer ──(SQL)──→ MySQL
 *   SuperServer   ──(REC_LOAD_USER_REQ)───→ RecordServer ──(SQL)──→ MySQL
 *   SceneServer   ──(REC_SAVE_USER_REQ)───→ RecordServer ──(SQL)──→ MySQL
 * @endcode
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/util/UserWireUtil.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/util/Singleton.h"
#include "../sdk/util/ServerList.h"
#include "../sdk/util/GameZoneExternSender.h"
#include "../protocal/InternalMsg.h"
#include "RecordUser.h"
#include "RecordUserManager.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <mysql/mysql.h>
#include "RelationStore.h"
#include <cinttypes>

/**
 * @brief RecordServer 核心类
 *
 * 单进程运行，持有 MySQL 连接，出站连接 SuperServer 注册。
 */
class RecordServer : public INetCallback, public LazySingleton<RecordServer>
{
    friend void RecordInternMsgRegister(RecordServer& server);
public:
    friend class LazySingleton<RecordServer>;
    /** @brief 获取 RecordServer 单例指针 */
    static RecordServer* Instance() { return &LazySingleton<RecordServer>::Instance(); }

private:
    /** @brief 构造 RecordServer（初始化网络/缓存状态） */
    RecordServer();

public:
    /** @brief 析构 RecordServer（关闭 DB 连接等） */
    ~RecordServer();

    /**
     * @brief 初始化 RecordServer
     * @param ip     监听 IP
     * @param port   监听端口（取自 ServerList 自身条目）
     * @param cfg    全局配置（含数据库连接参数、SuperServer 地址）
     * @param list   集群拓扑（用于解析对端地址与自身登记信息）
     * @param selfId 本进程实例编号
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg, const ServerList& list, uint32_t selfId);

    /** @brief 主循环 */
    void Run();

    /** @brief 内部连接建立 */
    void OnConnect(ConnID id) override;

    /** @brief 内部连接断开 */
    void OnDisconnect(ConnID id) override;

    /** @brief 收到消息后派发给 MsgDispatcher */
    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    /**
     * @brief 初始化 MySQL 连接
     * @param cfg 数据库配置
     * @return 成功返回 true
     */
    bool InitDB(const ServerConfig& cfg);

    /**
     * @brief 注册消息处理函数
     *
     * REC_LOAD_USER_REQ    → onLoadUser（从 DB 加载用户）
     * REC_SAVE_USER_REQ    → onSaveUser（保存用户到 DB）
     */
    void registerHandlers();

    /** @brief 向 SuperServer 注册 Record 节点 */
    void RegisterToSuper();

    /** @brief 定时上报心跳到 SuperServer */
    void sendHeartbeat();

    /**
     * @brief 加载用户数据
     *
     * 先从 m_users 缓存查找，未命中则调用 loadUserFromDb 从 MySQL 加载。
     */
    void onLoadUser(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 处理用户保存请求 */
    void onSaveUser(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Session 启动预载 Relation 全表 */
    void onRelationPreloadReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Session 单用户 Relation 加载 */
    void onRelationLoadReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Session Relation 保存 */
    void onRelationSaveReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Gateway loginToken 校验 */
    void onValidateTokenReq(ConnID fromConn, const Msg_REC_ValidateTokenReq& req);

    /** @brief LoginServer loginToken 校验回包（Super REC_VERIFY_TOKEN_RSP） */
    void onLoginVerifyTokenRsp(ConnID fromConn, const Msg_Login_VerifyTokenRsp& rsp);

    /** @brief 清理票据校验待回包超时上下文 */
    void CleanupPendingVerifyTokenTimeout();

    /** @brief Gateway 角色列表 */
    void onListCharactersReq(ConnID fromConn, const Msg_REC_ListCharactersReq& req);

    /** @brief Gateway 创角 */
    void onCreateCharacterReq(ConnID fromConn, const Msg_REC_CreateCharacterReq& req);

    /**
     * @brief 从 MySQL 加载用户数据到内存
     *
     * 查询 CharBase 表，构建 UserBase 并创建 RecordUser 对象。
     *
     * @note SELECT 字段与 row 索引映射表：
     *       | 索引 | 字段      | 类型      | 说明               |
     *       |------|-----------|-----------|--------------------|
     *       | row[0]  | user_id  | uint64_t  | 用户唯一标识       |
     *       | row[1]  | name     | string    | 用户昵称           |
     *       | row[2]  | level    | uint32_t  | 等级（默认 1）     |
     *       | row[3]  | vocation | uint32_t  | 职业（默认 0）     |
     *       | row[4]  | sex      | uint32_t  | 性别（默认 0）     |
     *       | row[5]  | map_id   | uint32_t  | 所在地图（默认 0） |
     *       | row[6]  | pos_x    | float     | X 坐标            |
     *       | row[7]  | pos_y    | float     | Y 坐标            |
     *       | row[8]  | pos_z    | float     | Z 坐标            |
     *       | row[9]  | hp       | uint32_t  | 当前生命值（默认 100）|
     *       | row[10] | mp       | uint32_t  | 当前魔法值（默认 100）|
     *       | row[11] | gold     | uint64_t  | 金币              |
     */
    void loadUserFromDb(UserID rid);

    /**
     * @brief 将用户数据写回 MySQL
     *
     * INSERT/UPDATE CharBase（user_id 主键）...
     */
    void saveUserToDb(UserID rid);

    /**
     * @brief 批量自动存档
     *
     * 每 60 秒定时触发，仅 saveUserToDb 中 needSave()==true 的用户。
     */
    void autoSaveAll();

    /** @brief Super 出站断线时尝试重连并重新注册 */
    void tryReconnectSuper();

    /** @brief 从有界队列取出一条写库任务执行（每主循环一次） */
    void drainSaveQueue();

    /** @brief 从有界队列取出一条读库任务并回包 Super（每主循环一次） */
    void drainLoadQueue();

    /**
     * @brief 向 Super 回发 REC_LOAD_USER_RSP
     * @param rid 用户 ID
     * @param requestSeq 回显请求序号
     */
    void sendLoadUserRsp(UserID rid, uint32_t requestSeq);

    /** @brief Super 不可达时失败所有待校验票据 */
    void failAllPendingVerifyTokens();

    /** @brief Record 发往 Login 校验票据的待回包上下文 */
    struct PendingVerifyToken
    {
        ConnID replyConn = INVALID_CONN_ID; /**< 需回 REC_VALIDATE_TOKEN_RSP 的连接 */
        uint32_t gatewayConnID = 0;         /**< Gateway 客户端连接 ID（回显） */
        uint64_t createdAtMs = 0;           /**< 发起校验时间（超时清理） */
    };

    TcpServer  m_server;          /**< 入站监听（Gateway / Scene / Session / Super） */
    TcpClient  m_superClient;     /**< 出站 SuperServer（注册、心跳） */
    MYSQL*     m_db;              /**< MySQL 连接句柄（全局共享，非线程安全） */
    uint32_t   m_hbSeq = 0;       /**< 心跳序列号（每次自增，SuperServer 用于检测丢包） */
    ServerEntry m_self;           /**< 本进程在 ServerList 中的拓扑条目（注册上报用） */
    RecordUserManager m_userManager;  /**< Record 用户缓存（userID -> RecordUser） */
    GameZoneExternSender m_externSender; /**< 经 Super 转发 Logger */
    std::string m_superIP;           /**< Super 地址（重连用） */
    uint16_t m_superPort = 0;        /**< Super 端口 */
    uint32_t m_loginVerifySeq = 0; /**< LOGIN_VERIFY_TOKEN_REQ 请求序号（发送时前置自增，首包序号为 1） */
    std::unordered_map<uint32_t, PendingVerifyToken> m_pendingVerifyToken; /**< seq -> 上下文 */

    /** @brief 异步写库队列项 */
    enum class SaveQueueKind : uint8_t
    {
        USER = 0,
        RELATION = 1,
    };

    struct SaveQueueItem
    {
        SaveQueueKind kind = SaveQueueKind::USER;
        UserID userId = INVALID_USER_ID;
        ConnID replyConn = INVALID_CONN_ID;
        std::vector<char> payload;
    };

    static constexpr size_t MAX_SAVE_QUEUE_DEPTH = 8192;
    std::deque<SaveQueueItem> m_saveQueue; /**< 有界写库队列 */

    /** @brief 异步读库队列项 */
    struct LoadQueueItem
    {
        UserID userId = INVALID_USER_ID;
        uint32_t requestSeq = 0;
    };

    static constexpr size_t MAX_LOAD_QUEUE_DEPTH = 4096;
    std::deque<LoadQueueItem> m_loadQueue; /**< 有界读库队列 */
    std::unordered_set<UserID> m_pendingLoadUsers; /**< 已在队列中的 userID（去重） */
    uint32_t m_superRetryDelayMs = 1000;   /**< Super 重连退避毫秒 */
    uint64_t m_superNextRetryMs = 0;       /**< 下次允许重连时刻 */
    uint64_t m_superTlsStuckSinceMs = 0;   /**< TLS 半开检测起点 */
};
