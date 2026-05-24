/**
 * @file    InternalMsg.h
 * @brief  服务器内部消息协议定义
 *
 * 微服务架构中，各服务器进程通过 TCP 长连接发送此文件定义的消息进行通信。
 * 协议号按责任服务器分段：
 *
 * | 范围               | 归属服务器      |
 * |--------------------|---------------|
 * | 0x1F01 ~ 0x1F04    | 注册/心跳（所有服务器共用） |
 * | 0x1001 ~ 0x1003    | SuperServer   |
 * | 0x1101 ~ 0x1105    | SessionServer |
 * | 0x1201 ~ 0x1206    | RecordServer  |
 * | 0x1301 ~ 0x1306    | SceneServer   |
 * | 0x1401 ~ 0x1405    | GatewayServer |
 * | 0x1501 ~ 0x1504    | AOIServer     |
 * | 0x1601             | LoggerServer  |
 * | 0x1701 ~ 0x1702    | GlobalServer  |
 * | 0x1801 ~ 0x1803    | ZoneServer    |
 *
 * 消息流示例（登录）：
 * @code
 *   Client → GatewayServer                           (C2S_LOGIN_REQ)
 *   GatewayServer → RecordServer                     (REC_LOGIN_VERIFY_REQ)
 *   RecordServer → GatewayServer                     (REC_LOGIN_VERIFY_RSP)
 *   GatewayServer → SuperServer                      (GW_USER_LOGIN_REQ)
 *   SuperServer → RecordServer                       (REC_LOAD_USER_REQ)
 *   RecordServer → SuperServer                       (REC_LOAD_USER_RSP)
 *   SuperServer → SceneServer                        (SCE_USER_ENTER_REQ)
 *   SceneServer → AOIServer                          (AOI_ENTER_REQ)
 *   SceneServer → GatewayServer                      (SCE_USER_ENTER_RSP)
 *   GatewayServer → Client                           (S2C_LOGIN_RSP / S2C_ENTER_GAME)
 * @endcode
 */

#pragma once
#include <cstdint>

/**
 * @brief 子服务器类型（与 Msg_S2S_Register::serverType 对应）
 */
enum class SubServerType : uint8_t
{
    UNKNOWN = 0,
    SESSION = 1,
    RECORD  = 2,
    AOI     = 3,
    SCENE   = 4,
    GATEWAY = 5,
    LOGGER  = 6,
    GLOBAL  = 7,
    ZONE    = 8,
};

/**
 * @brief 服务器内部消息协议号枚举
 */
enum class InternalMsgID : uint16_t
{
    // ============================================================
    //  服务器注册/心跳（所有服务器共用）0x1F01 ~ 0x1F04
    // ============================================================
    S2S_REGISTER_REQ     = 0x1F01,  /**< 子服务器 → SuperServer: 注册请求 */
    S2S_REGISTER_RSP     = 0x1F02,  /**< SuperServer → 子服务器: 注册响应 */
    S2S_HEARTBEAT        = 0x1F03,  /**< 子服务器 → SuperServer: 心跳 */
    S2S_HEARTBEAT_ACK    = 0x1F04,  /**< SuperServer → 子服务器: 心跳确认 */

    // ============================================================
    //  SuperServer (0x1001 ~ 0x1003)
    // ============================================================
    SS_KICK_USER         = 0x1001,  /**< 强制用户下线 */
    SS_QUERY_ONLINE      = 0x1002,  /**< 查询在线状态 */
    SS_QUERY_ONLINE_RSP  = 0x1003,  /**< 在线状态响应 */

    // ============================================================
    //  SessionServer (0x1101 ~ 0x1105)
    // ============================================================
    SES_LOAD_USER_REQ    = 0x1101,  /**< 加载用户社会关系数据 */
    SES_LOAD_USER_RSP    = 0x1102,  /**< 社会关系数据响应 */
    SES_SAVE_USER_REQ    = 0x1103,  /**< 保存用户社会关系数据 */
    SES_FRIEND_UPDATE    = 0x1104,  /**< 好友关系更新 */
    SES_OFFLINE_MSG_PUSH = 0x1105,  /**< 推送离线消息 */

    // ============================================================
    //  RecordServer (0x1201 ~ 0x1206)
    // ============================================================
    REC_LOAD_USER_REQ    = 0x1201,  /**< 从 DB 加载用户数据 */
    REC_LOAD_USER_RSP    = 0x1202,  /**< 用户数据加载响应 */
    REC_SAVE_USER_REQ    = 0x1203,  /**< 保存用户数据到 DB */
    REC_SAVE_USER_RSP    = 0x1204,  /**< 保存结果响应 */
    REC_LOGIN_VERIFY_REQ = 0x1205,  /**< 账号密码验证请求 */
    REC_LOGIN_VERIFY_RSP = 0x1206,  /**< 验证结果响应 */

    // ============================================================
    //  SceneServer (0x1301 ~ 0x1306)
    // ============================================================
    SCE_USER_ENTER_REQ   = 0x1301,  /**< 用户进入场景请求 */
    SCE_USER_ENTER_RSP   = 0x1302,  /**< 用户进入场景响应 */
    SCE_USER_LEAVE       = 0x1303,  /**< 用户离开场景通知 */
    SCE_FORWARD_TO_CLIENT= 0x1304,  /**< 向客户端转发消息（经 GatewayServer） */
    SCE_MAP_INFO_REQ     = 0x1305,  /**< 请求地图信息 */
    SCE_MAP_INFO_RSP     = 0x1306,  /**< 地图信息响应 */

    // ============================================================
    //  GatewayServer (0x1401 ~ 0x1405)
    // ============================================================
    GW_CLIENT_MSG        = 0x1401,  /**< 来自客户端的消息（转发给 SceneServer） */
    GW_SEND_TO_CLIENT    = 0x1402,  /**< SceneServer → Gateway: 发送给客户端 */
    GW_KICK_CLIENT       = 0x1403,  /**< 踢除客户端连接 */
    GW_USER_LOGIN_REQ    = 0x1404,  /**< Gateway → SuperServer: 发起用户登录流程 */
    GW_USER_LOGIN_RSP    = 0x1405,  /**< SuperServer → Gateway: 登录流程结果 */

    // ============================================================
    //  AOIServer (0x1501 ~ 0x1504)
    // ============================================================
    AOI_ENTER_REQ        = 0x1501,  /**< SceneServer → AOI: 实体进入视野管理 */
    AOI_LEAVE_REQ        = 0x1502,  /**< SceneServer → AOI: 实体离开视野管理 */
    AOI_MOVE_REQ         = 0x1503,  /**< SceneServer → AOI: 实体移动更新 */
    AOI_VIEW_NOTIFY      = 0x1504,  /**< AOI → SceneServer: 视野变化通知 */

    // ============================================================
    //  LoggerServer (0x1601)
    // ============================================================
    LOG_WRITE_REQ        = 0x1601,  /**< 远程写日志请求 */

    // ============================================================
    //  GlobalServer (0x1701 ~ 0x1702)
    // ============================================================
    GLB_DATA_SYNC        = 0x1701,  /**< 全区数据同步 */
    GLB_RANK_UPDATE      = 0x1702,  /**< 排行榜更新 */

    // ============================================================
    //  ZoneServer (0x1801 ~ 0x1803)
    // ============================================================
    ZONE_CROSS_REQ       = 0x1801,  /**< 跨区请求 */
    ZONE_CROSS_RSP       = 0x1802,  /**< 跨区响应 */
    ZONE_FORWARD         = 0x1803,  /**< 跨区转发 */
};

// ============================================================
//  服务器内部消息结构体
//
//  所有结构体使用 #pragma pack(push, 1) 保证紧密排列，
//  避免不同编译器/平台的对齐差异。
// ============================================================
#pragma pack(push, 1)

/**
 * @brief 子服务器向 SuperServer 注册
 *
 * 所有依赖服务器启动后必须发送此消息，SuperServer 据此建立路由表。
 *
 * 注册流程：
 * 1. 子服务器启动后，主动向 SuperServer 发送 S2S_REGISTER_REQ（Msg_S2S_Register）。
 * 2. SuperServer 收到后校验 serverType/serverID 合法性，记录该服务器的 IP:Port 到路由表。
 * 3. SuperServer 回复 S2S_REGISTER_RSP，携带分配的 logicalNodeID。
 * 4. 注册成功后，子服务器开始周期性发送 S2S_HEARTBEAT 以维持在线状态。
 * 5. 若 SuperServer 在 N 个心跳周期内未收到某子服务器的心跳，则将其标记为离线，
 *    并通知相关服务器进行故障转移（如 SceneServer 上的用户踢出等）。
 */
struct Msg_S2S_Register
{
    uint8_t  serverType;  /**< 服务器类型（对应 SubServerType 枚举值） */
    uint32_t serverID;    /**< 服务器实例编号（用于负载均衡） */
    char     ip[32];      /**< 服务器监听 IP（空终止字符串） */
    uint16_t port;        /**< 服务器监听端口 */
};

/**
 * @brief 心跳消息（请求与响应共用此结构）
 *
 * 超时机制说明：
 * - 子服务器每隔 HEARTBEAT_INTERVAL（默认 5 秒）向 SuperServer 发送 S2S_HEARTBEAT。
 * - SuperServer 收到后立即回复 S2S_HEARTBEAT_ACK。
 * - 若 SuperServer 在 HEARTBEAT_TIMEOUT（默认 15 秒，即 3 个周期）内未收到心跳，
 *   则判定该子服务器离线，触发以下动作：
 *     1. 从路由表中移除该服务器条目。
 *     2. 广播服务器离线通知给所有已注册的子服务器。
 *     3. 若离线的是 SceneServer，SuperServer 会通知 GatewayServer 断开该场景上
 *        所有用户的客户端连接（通过 GW_KICK_CLIENT）。
 * - seq 字段用于检测丢包和乱序：接收方若发现 seq 不连续，可记录告警日志。
 */
struct Msg_S2S_Heartbeat
{
    uint32_t seq;       /**< 序列号（自增） */
    uint64_t timestamp; /**< 发送时间戳（毫秒） */
};

/**
 * @brief GatewayServer → RecordServer: 登录验证请求
 */
struct Msg_REC_LoginVerifyReq
{
    char     account[32];      /**< 账号 */
    char     password[32];     /**< 密码 */
    uint32_t gatewayConnID;    /**< GatewayServer 中该客户端的连接 ID（用于回包路由） */
};

/**
 * @brief RecordServer → GatewayServer: 登录验证响应
 */
struct Msg_REC_LoginVerifyRsp
{
    int32_t  code;           /**< 0=成功, 1=密码错误, -1=服务器错误 */
    uint64_t userID;         /**< 验证通过时的用户 ID */
    uint32_t gatewayConnID;  /**< 回显 GatewayServer 连接 ID */
};

/**
 * @brief RecordServer 加载用户数据响应
 *
 * code=0 时后续追加完整的用户二进制数据（UserBase 序列化 + 扩展字段）。
 */
struct Msg_REC_LoadUserRsp
{
    int32_t  code;      /**< 0=成功, -1=用户不存在 */
    uint64_t userID;    /**< 用户 ID */
    // 成功时后续追加 UserBaseWire ...
};

/**
 * @brief 用户基础数据网络传输格式（定长，可 memcpy 序列化）
 *
 * 序列化/反序列化说明：
 * - 此结构体使用 #pragma pack(push, 1) 保证紧密排列，无填充字节。
 * - 序列化：直接将结构体指针 reinterpret_cast 为 char* 后 memcpy 到发送缓冲区，
 *   按字段声明顺序逐字节写入，无需额外编码。
 * - 反序列化：从接收缓冲区 memcpy 到此结构体即可恢复，建议先校验缓冲区长度
 *   不小于 sizeof(UserBaseWire) 以防止越界读取。
 * - 字符串字段（如 name）使用定长 char 数组，空终止字符串，不足部分补零。
 * - 数值字段使用网络字节序（大端）传输，发送/接收时需调用 hton*/ntoh* 转换。
 * - 若需扩展字段，应在结构体末尾追加并同步更新版本号，确保向后兼容。
 */
struct UserBaseWire
{
    uint64_t userID   = 0;                /**< 全局唯一用户 ID（8 字节大端，与 DB 主键一致） */
    char     name[32] = {};               /**< 用户昵称（空终止字符串，最多 31 个有效字符） */
    uint32_t level    = 1;                /**< 等级（默认 1，范围 1~999） */
    uint32_t vocation = 0;                /**< 职业类型（0=战士 1=法师 2=弓手 3=刺客） */
    uint32_t sex      = 0;                /**< 性别（0=男 1=女） */
    uint32_t mapID    = 0;                /**< 当前所在地图 ID（0 表示无地图） */
    float    posX     = 0.f;              /**< X 轴坐标（世界坐标系，浮点精度） */
    float    posY     = 0.f;              /**< Y 轴坐标（高度） */
    float    posZ     = 0.f;              /**< Z 轴坐标 */
    uint32_t hp       = 100;              /**< 当前生命值 */
    uint32_t maxHP    = 100;              /**< 最大生命值 */
    uint32_t mp       = 100;              /**< 当前魔法值 */
    uint32_t maxMP    = 100;              /**< 最大魔法值 */
    uint64_t gold     = 0;                /**< 金币数量 */
};

/**
 * @brief SuperServer → SceneServer: 用户进入场景请求
 */
struct Msg_SCE_UserEnterReq
{
    uint64_t userID;              /**< 要进入场景的用户 ID */
    uint32_t mapID;               /**< 目标地图 ID */
    float    x, y, z;             /**< 出生点坐标 */
    uint32_t gatewayClientConnID; /**< Gateway 中客户端连接 ID */
    char     name[32];            /**< 用户名 */
    uint32_t level    = 1;
    uint32_t vocation = 0;
    uint32_t sex      = 0;
    uint32_t hp       = 100;
    uint32_t maxHP    = 100;
    uint32_t mp       = 100;
    uint32_t maxMP    = 100;
    uint64_t gold     = 0;
};

/**
 * @brief SceneServer → SuperServer: 用户进入场景结果
 */
struct Msg_SCE_UserEnterRsp
{
    int32_t  code;                /**< 0=成功, -1=失败 */
    uint64_t userID;
    uint32_t gatewayClientConnID;
    uint32_t mapID;
};

/**
 * @brief SuperServer → GatewayServer: 登录流程完成通知
 */
struct Msg_GW_UserLoginRsp
{
    int32_t  code;                /**< 0=成功 */
    uint32_t gatewayClientConnID;
    uint64_t userID;
    uint32_t mapID;
    float    x, y, z;
    char     name[32];
    uint32_t level = 1;
    uint32_t hp    = 100;
    uint32_t maxHP = 100;
    uint32_t mp    = 100;
    uint32_t maxMP = 100;
};

/**
 * @brief GatewayServer → SceneServer: 客户端消息转发
 *
 * 后跟 dataLen 字节的原始消息体。
 */
struct Msg_GW_ClientMsg
{
    uint32_t clientConnID;  /**< GatewayServer 中的客户端连接 ID */
    uint16_t msgID;         /**< 原始客户端协议号 */
    uint16_t dataLen;       /**< 消息体长度 */
    // 后跟 dataLen 字节的消息体 ...
};

/**
 * @brief AOI 移动/进入/离开 共用结构
 *
 * SceneServer 通过此结构将实体位置变更通知 AOIServer。
 */
struct Msg_AOI_Move
{
    uint64_t entityID;  /**< 实体 ID（玩家/NPC/怪物） */
    uint32_t mapID;     /**< 所在地图 ID */
    float    x, y, z;   /**< 坐标 */
    float    dir;       /**< 朝向 */
};

#pragma pack(pop)
