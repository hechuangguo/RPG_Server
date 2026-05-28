/**
 * @file    RecordServer.h
 * @brief  数据库服务器 —— 用户数据持久化（MySQL）、账号登录验证
 *
 * ## 职责
 * - MySQL 直连：账号验证、用户数据加载/保存
 * - 定时自动存档（每 60 秒 AutoSaveAll）
 * - 内部通信：连接 SuperServer + SessionServer
 *
 * ## 依赖关系
 * - 依赖 SuperServer（注册） + SessionServer（社会关系数据）
 * - 被 GatewayServer（登录验证）、SuperServer（加载用户）、SceneServer（保存用户）调用
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
#include "../protocal/InternalMsg.h"
#include "RecordUser.h"
#include "RecordUserManager.h"
#include <string>
#include <vector>
#include <mysql/mysql.h>
#include <cinttypes>

/**
 * @brief RecordServer 核心类
 *
 * 单进程运行，持有 MySQL 连接，同时维护到 SuperServer 和 SessionServer 的 TcpClient。
 */
class RecordServer : public INetCallback
{
public:
    /** @brief 构造 RecordServer（初始化网络/缓存状态） */
    RecordServer();

    /** @brief 析构 RecordServer（关闭 DB 连接等） */
    ~RecordServer();

    /**
     * @brief 初始化 RecordServer
     * @param ip   监听 IP
     * @param port 监听端口
     * @param cfg  全局配置（含数据库连接参数）
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg);

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
     * @brief 账号密码验证
     *
     * 使用 SELECT ... WHERE account='?' AND password=MD5('?') 查询 t_account 表。
     * @note init.sql 当前无账号表；登录逻辑待与 CharBase 或 Account 表对齐。
     *
     * @warning 【安全风险】当前实现使用 snprintf 拼接 SQL 字符串，存在 SQL 注入风险。
     *          如果 req->account 或 req->password 中包含单引号等特殊字符，
     *          攻击者可构造恶意输入绕过认证或执行任意 SQL。
     *          生产环境必须使用 mysql_real_escape_string() 转义用户输入，
     *          或改用 mysql_stmt_prepare() / mysql_stmt_bind_param() 参数化查询。
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
    TcpServer  m_server;          /**< 内部连接监听（接收来自 Gateway/Super/Scene Server 的请求） */
    TcpClient  m_superClient;     /**< 到 SuperServer 的连接（用于注册服务器、发送心跳） */
    TcpClient  m_sessionClient;   /**< 到 SessionServer 的连接（用于查询社会关系/好友数据） */
    MYSQL*     m_db;              /**< MySQL 连接句柄（全局共享，非线程安全） */
    uint32_t   m_hbSeq = 0;       /**< 心跳序列号（每次自增，SuperServer 用于检测丢包） */
    RecordUserManager m_userManager;  /**< Record 用户缓存（userID -> RecordUser） */
};
