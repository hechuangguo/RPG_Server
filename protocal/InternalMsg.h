/**
 * @file    InternalMsg.h
 * @brief  服务器内部消息协议定义
 *
 * 微服务架构中，各服务器进程通过 TCP 长连接发送此文件定义的消息进行通信。
 * 线上帧：MsgHeader(6B: bodyLen + module + sub) + body，与客户端共用 sdk/net/NetDefine.h。
 * 协议号按责任服务器分段（扁平 ID 高字节多为 module）：
 *
 * | 范围               | 归属服务器      |
 * |--------------------|---------------|
 * | 0x1F01 ~ 0x1F04    | 注册/心跳（所有服务器共用） |
 * | 0x1001 ~ 0x1003    | SuperServer   |
 * | 0x1101 ~ 0x1105    | SessionServer |
 * | 0x1201 ~ 0x1212    | RecordServer  |
 * | 0x1301 ~ 0x1306    | SceneServer   |
 * | 0x1401 ~ 0x1405    | GatewayServer |
 * | 0x1501 ~ 0x1504    | AOIServer     |
 * | 0x1601             | LoggerServer  |
 * | 0x1701 ~ 0x1702    | GlobalServer  |
 * | 0x1801 ~ 0x1803    | ZoneServer    |
 * | 0x1901 ~ 0x1909    | LoginServer   |
 * | 0x1F10 ~ 0x1F13    | SuperServer 外联转发 |
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
    UNKNOWN = 0, /**< 未识别类型（非法或未初始化） */
    SESSION = 1, /**< SessionServer：全局会话与场景调度 */
    RECORD  = 2, /**< RecordServer：唯一 DB 落库与读档入口 */
    AOI     = 3, /**< AOIServer：视野管理与可见性计算 */
    SCENE   = 4, /**< SceneServer：地图与战斗等玩法逻辑 */
    GATEWAY = 5, /**< GatewayServer：客户端接入与协议校验 */
    LOGGER  = 6, /**< LoggerServer：集中日志写入 */
    GLOBAL  = 7, /**< GlobalServer：全区共享业务 */
    ZONE    = 8, /**< ZoneServer：跨区转发与互通 */
    LOGIN   = 9, /**< LoginServer：外联登录验证与网关列表（不入 DB ServerList） */
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
    S2S_SERVERLIST_REQ   = 0x1F05,  /**< 子服务器 → SuperServer: 启动期拉取集群拓扑 ServerList */
    S2S_SERVERLIST_RSP   = 0x1F06,  /**< SuperServer → 子服务器: 返回 ServerList 全量条目 */

    /** @brief SuperServer 外联转发：区内服 → Super（信封 + inner body） */
    SS_EXTERN_FWD_REQ    = 0x1F10,
    /** @brief SuperServer 外联转发：Super → 区内服（响应） */
    SS_EXTERN_FWD_RSP    = 0x1F11,
    /** @brief SuperServer → 独立服：游戏区转发请求（信封 + inner body） */
    EXT_GAMEZONE_FWD_REQ = 0x1F12,
    /** @brief 独立服 → SuperServer：游戏区转发响应 */
    EXT_GAMEZONE_FWD_RSP = 0x1F13,

    /** @brief Gateway → Super：网关注册包装（含 gatewayConnID） */
    SS_LOGIN_GATEWAY_WRAP_REQ = 0x1F14,
    /** @brief Super → Gateway：网关注册响应包装 */
    SS_LOGIN_GATEWAY_WRAP_RSP = 0x1F15,

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
    SES_SCENE_REGISTER_REQ = 0x1106, /**< SceneServer → Session: 普通/副本场景注册 */
    SES_SCENE_REGISTER_RSP = 0x1107, /**< Session → SceneServer: 注册响应 */
    SES_SCENE_UNREGISTER   = 0x1108, /**< SceneServer → Session: 场景注销 */
    SES_COPY_CREATE_REQ    = 0x1109, /**< SceneServer → Session: 请求创建副本 */
    SES_COPY_CREATE_RSP    = 0x1110, /**< Session → SceneServer: 副本分配结果 */
    SES_COPY_CREATE_CMD    = 0x1111, /**< Session → SceneServer: 在目标进程创建副本 */
    SES_RESOLVE_MAP_REQ    = 0x1112, /**< Super → Session: 按 mapId 解析 sceneServerId */
    SES_RESOLVE_MAP_RSP    = 0x1113, /**< Session → Super: mapId 解析结果 */

    // ============================================================
    //  RecordServer (0x1201 ~ 0x1206)
    // ============================================================
    REC_LOAD_USER_REQ    = 0x1201,  /**< 从 DB 加载用户数据 */
    REC_LOAD_USER_RSP    = 0x1202,  /**< 用户数据加载响应 */
    REC_SAVE_USER_REQ    = 0x1203,  /**< 保存用户数据到 DB */
    REC_SAVE_USER_RSP    = 0x1204,  /**< 保存结果响应 */
    REC_LOGIN_VERIFY_REQ = 0x1205,  /**< 账号密码验证请求 */
    REC_LOGIN_VERIFY_RSP = 0x1206,  /**< 验证结果响应 */
    REC_RELATION_PRELOAD_REQ = 0x1207, /**< SessionServer → RecordServer: 启动预载 Relation 全表 */
    REC_RELATION_PRELOAD_RSP = 0x1208, /**< RecordServer → SessionServer: 预载响应（变长多行） */
    REC_RELATION_LOAD_REQ    = 0x1209, /**< SessionServer → RecordServer: 单用户 Relation 加载 */
    REC_RELATION_LOAD_RSP    = 0x120A, /**< RecordServer → SessionServer: 单用户 Relation 响应 */
    REC_RELATION_SAVE_REQ    = 0x120B, /**< SessionServer → RecordServer: 保存 Relation 行 */
    REC_RELATION_SAVE_RSP    = 0x120C, /**< RecordServer → SessionServer: 保存结果 */

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
    AOI_SCENE_REGISTER   = 0x1505,  /**< SceneServer → AOI: 场景注册 */
    AOI_SCENE_UNREGISTER = 0x1506,  /**< SceneServer → AOI: 场景注销 */

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

    // ============================================================
    //  LoginServer (0x1901 ~ 0x1903)
    // ============================================================
    LOGIN_GATEWAY_REGISTER_REQ = 0x1901, /**< Super → LoginServer: 上报网关 */
    LOGIN_GATEWAY_REGISTER_RSP = 0x1902, /**< LoginServer → Super: 注册确认 */
    LOGIN_GATEWAY_HEARTBEAT    = 0x1903, /**< Super → LoginServer: 网关存活心跳 */
    LOGIN_RECHARGE_REQ         = 0x1904, /**< 充值请求（骨架，经 SS_EXTERN_FWD） */
    LOGIN_GM_CMD_REQ           = 0x1905, /**< GM 指令（骨架，经 SS_EXTERN_FWD） */
    LOGIN_ZONE_STATUS_REPORT   = 0x1906, /**< Super → LoginServer: 游戏区状态上报 */
};

/** @brief LoginServer 业务大类（骨架） */
enum class LoginBizType : uint16_t
{
    RECHARGE = 1, /**< 充值 */
    GM_CMD   = 2, /**< GM 工具 */
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
    char     name[32];    /**< 服务器名（来自 ServerList，便于日志/运维识别） */
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
    uint32_t seq;         /**< 序列号（自增） */
    uint64_t timestamp;   /**< 发送时间戳（毫秒） */
    uint32_t onlineCount; /**< Gateway：客户端会话数；其它子服填 0 */
};

/**
 * @brief 子服务器 → SuperServer: 启动期拉取集群拓扑请求
 *
 * 触发时机：子服务器（Session/Record/AOI/Scene/Gateway）启动、连上 SuperServer 后，
 * 发送本请求获取 ServerList；据此确定自身监听端口与对端地址，再绑定/连接/注册。
 */
struct Msg_S2S_ServerListReq
{
    uint8_t  serverType;  /**< 请求方服务器类型（对应 SubServerType） */
    uint32_t serverID;    /**< 请求方服务器实例编号 */
};

/**
 * @brief ServerList 单条目（集群中一个服务器进程的拓扑信息）
 *
 * 用于 S2S_SERVERLIST_RSP 的变长数组元素，与 DB 表 ServerList 字段一一对应。
 */
struct Msg_ServerEntry
{
    uint32_t serverID;    /**< 服务器实例编号 */
    uint8_t  serverType;  /**< 服务器类型（对应 SubServerType） */
    char     ip[32];      /**< 监听 IP（空终止字符串） */
    uint16_t port;        /**< 监听端口 */
    char     name[32];    /**< 服务器名 */
};

/**
 * @brief SuperServer → 子服务器: ServerList 全量响应（变长）
 *
 * 线上布局：本头部（count）+ 紧随其后的 count 个 Msg_ServerEntry。
 * 接收方按 count 顺序解析后续 count*sizeof(Msg_ServerEntry) 字节。
 */
struct Msg_S2S_ServerListRsp
{
    uint16_t count;       /**< 后续 Msg_ServerEntry 条目数量 */
    // 紧随其后：Msg_ServerEntry entries[count];
};

/**
 * @brief 游戏区进程 → LoggerServer: 远程日志写入请求（变长）
 *
 * 线上布局：本头部 + 紧随其后的 logLen 字节纯文本（可含换行）。
 * LoggerServer 按 serverType 分文件落盘。
 */
struct Msg_Log_WriteReq
{
    uint8_t  serverType;  /**< 来源服务器类型（SubServerType 枚举值） */
    uint8_t  level;       /**< 日志级别：0=DEBUG 1=INFO 2=WARN 3=ERR 4=FATAL */
    uint32_t logLen;      /**< 日志文本长度（字节） */
    // 紧随其后：char logText[logLen];
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
 * - 数值字段使用网络字节序（大端）传输，发送/接收时需调用 hton 与 ntoh 系列函数转换。
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
 * @brief SceneServer → RecordServer: 保存 charbase（含完整在线属性）
 */
struct Msg_REC_SaveUserReq
{
    uint64_t     userID; /**< 需要保存的用户 ID */
    UserBaseWire wire;   /**< 完整基础属性快照 */
};

/**
 * @brief SessionServer → RecordServer: 启动期预载 Relation（body 可为空）
 */
struct Msg_REC_RelationPreloadReq
{
    uint32_t reserved = 0; /**< 保留，填 0 */
};

/**
 * @brief RecordServer → SessionServer: Relation 预载响应头
 *
 * 成功时 body 紧随 count 条 RelationWireRow（见 RelationWireRow 编码说明）。
 */
struct Msg_REC_RelationPreloadRsp
{
    int32_t  code;   /**< 0=成功，非 0 失败 */
    uint32_t count;  /**< 紧随的行数 */
};

/**
 * @brief RecordServer → SessionServer: 单用户 Relation 加载响应头
 *
 * code=0 时 body 紧随一条 RelationWireRow。
 */
struct Msg_REC_RelationLoadRsp
{
    int32_t  code;    /**< 0=成功，-1=不存在或 DB 错误 */
    uint64_t userID;  /**< 请求的用户 ID */
};

/**
 * @brief RecordServer → SessionServer: Relation 保存结果
 */
struct Msg_REC_RelationSaveRsp
{
    int32_t  code;    /**< 0=成功 */
    uint64_t userID;  /**< 保存的用户 ID */
};

/**
 * @brief Relation 表单行线格式（预载/加载/保存变长 body 共用）
 *
 * 按序序列化（小端，紧密排列）：
 * | userID (8B) | friendsLen (4B) | blacklistLen (4B) | guildId (8B) |
 * | teamId (4B) | binaryLen (4B) | friends[friendsLen] | blacklist[blacklistLen] | binary[binaryLen] |
 *
 * friends/blacklist 为逗号分隔的 userId 文本（与 DB friends_json 列一致）。
 */
struct RelationWireRowHeader
{
    uint64_t userID       = 0;
    uint32_t friendsLen   = 0;
    uint32_t blacklistLen = 0;
    uint64_t guildId      = 0;
    uint32_t teamId       = 0;
    uint32_t binaryLen    = 0;
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
    uint32_t level    = 1;        /**< 用户等级快照 */
    uint32_t vocation = 0;        /**< 职业类型（与 UserBaseWire 一致） */
    uint32_t sex      = 0;        /**< 性别（与 UserBaseWire 一致） */
    uint32_t hp       = 100;      /**< 当前生命值 */
    uint32_t maxHP    = 100;      /**< 最大生命值 */
    uint32_t mp       = 100;      /**< 当前魔法值 */
    uint32_t maxMP    = 100;      /**< 最大魔法值 */
    uint64_t gold     = 0;        /**< 当前金币 */
};

/**
 * @brief SceneServer → SuperServer: 用户进入场景结果
 */
struct Msg_SCE_UserEnterRsp
{
    int32_t  code;                /**< 0=成功, -1=失败 */
    uint64_t userID;              /**< 用户 ID */
    uint32_t gatewayClientConnID; /**< Gateway 连接 ID（用于定位回包目标） */
    uint32_t mapID;               /**< 实际进入的地图 ID */
};

/**
 * @brief SuperServer → GatewayServer: 登录流程完成通知
 */
struct Msg_GW_UserLoginRsp
{
    int32_t  code;                /**< 0=成功 */
    uint32_t gatewayClientConnID; /**< Gateway 中客户端连接 ID */
    uint64_t userID;              /**< 用户 ID */
    uint32_t mapID;               /**< 登录后目标地图 ID */
    float    x, y, z;             /**< 登录出生点坐标 */
    char     name[32];            /**< 用户名 */
    uint32_t level = 1;           /**< 用户等级 */
    uint32_t hp    = 100;         /**< 当前生命值 */
    uint32_t maxHP = 100;         /**< 最大生命值 */
    uint32_t mp    = 100;         /**< 当前魔法值 */
    uint32_t maxMP = 100;         /**< 最大魔法值 */
    uint32_t sceneServerId = 0;   /**< 用户所在 SceneServer 实例 ID（ServerList.server_id） */
};

/**
 * @brief GatewayServer → LoginServer: 网关注册/心跳（LOGIN_GATEWAY_HEARTBEAT 复用本结构）
 */
struct Msg_Login_GatewayRegister
{
    uint32_t gatewayServerId; /**< 网关实例 ID（ServerList） */
    uint32_t zoneId;          /**< 所属游戏区号 */
    uint8_t  gameType;        /**< 游戏类型 */
    uint8_t  reserved[3];     /**< 对齐保留 */
    char     ip[32];          /**< 客户端可连 IP */
    uint16_t port;            /**< 客户端监听端口（如 9005） */
    char     name[32];        /**< 网关名称 */
    char     zoneName[32];    /**< 可选区服名 */
};

/**
 * @brief SuperServer → LoginServer: 游戏区状态周期上报（单向，无回包）
 *
 * 触发时机：Super 定时汇总各 Gateway 心跳中的 onlineCount 后推送。
 */
struct Msg_Login_ZoneStatusReport
{
    uint32_t zoneId;       /**< 游戏区号 */
    uint8_t  gameType;     /**< 游戏类型 */
    uint8_t  alive;        /**< 1=区可达且有存活网关，0=不可达 */
    uint8_t  reserved;     /**< 对齐保留 */
    uint32_t onlineCount;  /**< 全区在线人数（各 Gateway 之和） */
    uint32_t gatewayCount; /**< 近期有心跳的 Gateway 数量 */
};

/**
 * @brief LoginServer → GatewayServer: 网关注册结果
 */
struct Msg_Login_GatewayRegisterRsp
{
    int32_t  code;            /**< 0=成功 */
    uint32_t gatewayServerId; /**< 回显网关 ID */
};

/**
 * @brief Gateway → Super：网关注册/心跳包装（Super 再转发 LoginServer）
 */
struct Msg_SS_LoginGatewayWrap
{
    uint32_t gatewayConnID;              /**< Super 侧 Gateway 连接 ID */
    Msg_Login_GatewayRegister body;      /**< 网关信息 */
};

/**
 * @brief Super → Gateway：网关注册结果包装
 */
struct Msg_SS_LoginGatewayWrapRsp
{
    uint32_t gatewayConnID;              /**< 回显 Gateway 连接 ID */
    Msg_Login_GatewayRegisterRsp body;   /**< 注册结果 */
};

/**
 * @brief 外联转发信封（SS_EXTERN_FWD / EXT_GAMEZONE_FWD 共用头，后跟 inner body）
 */
struct Msg_SS_ExternForward
{
    uint8_t  sourceServerType;  /**< 发起方 SubServerType */
    uint32_t sourceServerId;    /**< 发起方 serverID */
    uint8_t  targetServerType;  /**< 目标 SubServerType（LOGIN/LOGGER/GLOBAL/ZONE 或回包目标） */
    uint16_t innerMsgId;        /**< 业务协议号 */
    uint32_t seq;               /**< 请求序号（配对 RSP，0 表示无需响应） */
    uint16_t dataLen;           /**< 后续 body 长度 */
};

/**
 * @brief 外联转发响应头（SS_EXTERN_FWD_RSP / EXT_GAMEZONE_FWD_RSP）
 */
struct Msg_SS_ExternForwardRsp
{
    uint8_t  sourceServerType;  /**< 原请求发起方类型 */
    uint32_t sourceServerId;    /**< 原请求发起方 ID */
    uint8_t  targetServerType;  /**< 原请求目标类型 */
    uint16_t innerMsgId;        /**< 原业务协议号 */
    uint32_t seq;               /**< 原请求序号 */
    int32_t  code;              /**< 0=成功 */
    uint16_t dataLen;           /**< 后续 body 长度 */
};

/**
 * @brief GatewayServer → SceneServer/SessionServer: 客户端消息转发
 *
 * 后跟 dataLen 字节的原始消息体（与客户端线上 body 一致）。
 */
struct Msg_GW_ClientMsg
{
    uint32_t clientConnID;  /**< Gateway 侧客户端连接 ID */
    uint8_t  module;        /**< 客户端 module */
    uint8_t  sub;           /**< 客户端 sub */
    uint16_t dataLen;       /**< 消息体长度 */
};

/**
 * @brief SceneServer/SessionServer → GatewayServer: 下行到客户端
 *
 * 后跟 dataLen 字节消息体。
 */
struct Msg_GW_SendToClient
{
    uint32_t clientConnID; /**< Gateway 侧客户端连接 ID */
    uint8_t  module;       /**< 客户端协议 module */
    uint8_t  sub;          /**< 客户端协议 sub */
    uint16_t dataLen;      /**< 后续原始包体长度 */
};

/**
 * @brief AOI 移动/进入/离开 共用结构
 *
 * SceneServer 通过此结构将实体位置变更通知 AOIServer。
 */
struct Msg_AOI_Move
{
    uint64_t entityID;   /**< 实体 ID（玩家/NPC/怪物） */
    uint32_t mapID;      /**< 所在地图 ID */
    float    x, y, z;    /**< 坐标 */
    float    dir;        /**< 朝向 */
    uint8_t  entityType; /**< 0=玩家 1=NPC 2=怪物（见 ClientMsg SpawnEntity） */
};

/** @brief 无效场景实例 ID */
constexpr uint64_t INVALID_SCENE_INSTANCE_ID = 0;

/**
 * @brief 生成普通场景实例 ID（高 32 位=SceneServer，低 32 位=mapId）
 * @param sceneServerId 场景进程编号
 * @param mapId 地图模板 ID
 * @return 可用于跨服路由的全局场景实例 ID
 */
inline uint64_t makeNormalSceneInstanceId(uint32_t sceneServerId, uint32_t mapId)
{
    return (static_cast<uint64_t>(sceneServerId) << 32) | mapId;
}

/** @brief 场景类型（普通 / 副本） */
enum class SceneKind : uint8_t
{
    NORMAL = 0, /**< 普通公共场景 */
    COPY   = 1, /**< 私有副本场景 */
};

/** @brief 场景运行状态 */
enum class SceneState : uint8_t
{
    CREATING = 0, /**< 创建中，尚未对外提供服务 */
    RUNNING  = 1, /**< 运行中，可接纳玩家 */
    CLOSING  = 2, /**< 关闭中，不再接收新玩家 */
    CLOSED   = 3, /**< 已关闭，可回收资源 */
};

/** @brief 副本类型（可扩展，工厂按类型创建子类） */
enum class CopyType : uint32_t
{
    TEAM  = 1,  /**< 组队副本 */
    SOLO  = 2,  /**< 单人副本 */
    GUILD = 3,  /**< 公会副本 */
};

/** @brief SceneServer → SessionServer：场景注册 */
struct Msg_SES_SceneRegisterReq
{
    uint32_t sceneServerId;   /**< 注册方 SceneServer ID */
    uint64_t sceneInstanceId; /**< 场景实例 ID（全局唯一） */
    uint32_t mapId;           /**< 地图模板 ID */
    uint8_t  sceneKind;       /**< 场景类型，取值见 SceneKind */
    char     mapName[32];     /**< 地图显示名 */
    char     mapFile[64];     /**< 地图资源文件名 */
    uint32_t maxPlayer;       /**< 场景容量上限 */
};

/** @brief SessionServer → SceneServer：场景注册结果 */
struct Msg_SES_SceneRegisterRsp
{
    int32_t  code;            /**< 0=成功，非 0=失败 */
    uint64_t sceneInstanceId; /**< 回显场景实例 ID */
};

/** @brief SceneServer → SessionServer：场景注销 */
struct Msg_SES_SceneUnregister
{
    uint64_t sceneInstanceId; /**< 待注销场景实例 ID */
    uint32_t sceneServerId;   /**< 发起注销的 SceneServer ID */
};

/** @brief SuperServer → SessionServer：按 mapId 解析承载该地图的 SceneServer */
struct Msg_SES_ResolveMapReq
{
    uint64_t userID; /**< 登录用户 ID（回包关联 pending） */
    uint32_t mapId;  /**< 地图模板 ID（分线固定默认，暂不扩展 lineId） */
};

/** @brief SessionServer → SuperServer：mapId 解析结果 */
struct Msg_SES_ResolveMapRsp
{
    int32_t  code;           /**< 0=成功，非 0=未找到可用场景 */
    uint64_t userID;         /**< 回显登录用户 ID */
    uint32_t mapId;          /**< 回显 mapId */
    uint32_t sceneServerId;  /**< 承载该 map 的 SceneServer 实例 ID */
};

/** @brief SceneServer → SessionServer：请求创建副本 */
struct Msg_SES_CopyCreateReq
{
    uint32_t reqSceneServerId; /**< 请求方 SceneServer ID */
    uint32_t copyType;         /**< 副本类型，取值见 CopyType */
    uint32_t mapId;            /**< 副本地图模板 ID */
    uint64_t ownerId;          /**< 副本拥有者用户 ID */
    uint32_t maxPlayer;        /**< 副本人数上限 */
    char     mapName[32];      /**< 地图显示名 */
    char     mapFile[64];      /**< 地图资源文件名 */
};

/** @brief SessionServer → SceneServer：副本创建/复用结果（回复请求方） */
struct Msg_SES_CopyCreateRsp
{
    int32_t  code;                /**< 0=成功，非 0=失败 */
    uint32_t targetSceneServerId; /**< 实际承载副本的 SceneServer ID */
    uint64_t copyInstanceId;      /**< 副本实例 ID */
    uint32_t copyType;            /**< 副本类型，取值见 CopyType */
    uint32_t mapId;               /**< 副本地图模板 ID */
    uint64_t ownerId;             /**< 副本拥有者用户 ID */
    uint32_t maxPlayer;           /**< 副本人数上限 */
    char     mapName[32];         /**< 地图显示名 */
    char     mapFile[64];         /**< 地图资源文件名 */
    uint8_t  reused;              /**< 1=复用已有副本，0=新建副本 */
};

/** @brief SessionServer → 目标 SceneServer：在本进程创建副本 */
struct Msg_SES_CopyCreateCmd
{
    uint64_t copyInstanceId; /**< 待创建副本实例 ID */
    uint32_t copyType;       /**< 副本类型，取值见 CopyType */
    uint32_t mapId;          /**< 副本地图模板 ID */
    uint64_t ownerId;        /**< 副本拥有者用户 ID */
    uint32_t maxPlayer;      /**< 副本人数上限 */
    char     mapName[32];    /**< 地图显示名 */
    char     mapFile[64];    /**< 地图资源文件名 */
};

/** @brief SceneServer → AOIServer：注册场景实例 */
struct Msg_AOI_SceneRegister
{
    uint32_t sceneServerId;   /**< 发起注册的 SceneServer ID */
    uint64_t sceneInstanceId; /**< 场景实例 ID */
    uint32_t mapId;           /**< 地图模板 ID */
    uint8_t  sceneKind;       /**< 场景类型，取值见 SceneKind */
    uint32_t maxPlayer;       /**< 场景容量上限 */
};

/** @brief SceneServer → AOIServer：注销场景实例 */
struct Msg_AOI_SceneUnregister
{
    uint64_t sceneInstanceId; /**< 待注销场景实例 ID */
};

#pragma pack(pop)
