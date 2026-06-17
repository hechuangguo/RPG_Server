/**
 * @file    RecordServer.h
 * @brief  数据库服务器 —— 用户数据持久化（MySQL）、账号登录验证
 *
 * ## 职责
 * - MySQL 直连：账号验证、用户数据加载/保存
 * - 定时自动存档（每 60 秒 AutoSaveAll）
 * - 内部通信：出站 SuperServer；入站 Gateway / Scene / Session
 *
 * ## 依赖关系
 * - 出站：SuperServer（注册）
 * - 入站：GatewayServer（登录验证）、SceneServer（存档）、SessionServer（Relation）
 * - 被 SuperServer（加载用户）经入站连接调用
 *
 * ## 数据流
 * @code
 *   GatewayServer ──(REC_LOGIN_VERIFY_REQ)──→ RecordServer ──(SQL)──→ MySQL
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
#include <vector>
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
     * REC_LOGIN_VERIFY_REQ → OnLoginVerify（账号密码验证）
     * REC_LOAD_USER_REQ    → OnLoadUser（从 DB 加载用户）
     * REC_SAVE_USER_REQ    → OnSaveUser（保存用户到 DB）
     */
    void RegisterHandlers();

    /** @brief 向 SuperServer 注册 Record 节点 */
    void RegisterToSuper();

    /** @brief 定时上报心跳到 SuperServer */
    void SendHeartbeat();

    /**
     * @brief 登录验证（按角色名）
     *
     * CharBase 已合表，无 account/password 列；故 account 视为角色名，
     * 执行 SELECT user_id FROM CharBase WHERE name='?'。
     * 未命中则首登自动建号（INSERT CharBase(name)，其余列走 init.sql 默认值），
     * 以 mysql_insert_id() 返回新角色 ID。req->password 暂不参与校验。
     *
     * @note account 入库前用 mysql_real_escape_string() 转义，规避 SQL 注入。
     */
    void OnLoginVerify(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 加载用户数据
     *
     * 先从 m_users 缓存查找，未命中则调用 LoadUserFromDB 从 MySQL 加载。
     */
    void OnLoadUser(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 处理用户保存请求 */
    void OnSaveUser(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Session 启动预载 Relation 全表 */
    void OnRelationPreloadReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Session 单用户 Relation 加载 */
    void OnRelationLoadReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Session Relation 保存 */
    void OnRelationSaveReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Gateway loginToken 校验 */
    void OnValidateTokenReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief LoginServer loginToken 校验回包 */
    void OnLoginVerifyTokenRsp(ConnID fromConn, const char* data, uint16_t len);

    /** @brief 清理票据校验待回包超时上下文 */
    void CleanupPendingVerifyTokenTimeout();

    /** @brief Gateway 角色列表 */
    void OnListCharactersReq(ConnID fromConn, const char* data, uint16_t len);

    /** @brief Gateway 创角 */
    void OnCreateCharacterReq(ConnID fromConn, const char* data, uint16_t len);

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
    void LoadUserFromDB(UserID rid);

    /**
     * @brief 将用户数据写回 MySQL
     *
     * INSERT/UPDATE CharBase（user_id 主键）...
     */
    void SaveUserToDB(UserID rid);

    /**
     * @brief 批量自动存档
     *
     * 每 60 秒定时触发，遍历所有 m_users 调用 SaveUserToDB。
     *
     * @note 脏标记检查逻辑：当前实现无条件保存全部用户数据。
     *       若需优化 I/O 开销，可增加脏标记（dirty flag）过滤：
     *       仅当用户数据被修改（dirty == true）时才执行 SaveUserToDB，
     *       保存成功后将 dirty 重置为 false。
     */
    void AutoSaveAll();

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
    uint32_t m_loginVerifySeq = 0; /**< LOGIN_VERIFY_TOKEN_REQ 请求序号（发送时前置自增，首包序号为 1） */
    std::unordered_map<uint32_t, PendingVerifyToken> m_pendingVerifyToken; /**< seq -> 上下文 */
};
