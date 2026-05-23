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
 *   GatewayServer → SuperServer                      (GW_ROLE_LOGIN_REQ)
 *   SuperServer → RecordServer                       (REC_LOAD_ROLE_REQ)
 *   RecordServer → SuperServer                       (REC_LOAD_ROLE_RSP)
 *   SuperServer → SceneServer                        (SCE_ROLE_ENTER_REQ)
 *   SceneServer → AOIServer                          (AOI_ENTER_REQ)
 *   SceneServer → GatewayServer                      (SCE_ROLE_ENTER_RSP)
 *   GatewayServer → Client                           (S2C_LOGIN_RSP / S2C_ENTER_GAME)
 * @endcode
 */

#pragma once
#include <cstdint>

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
    SS_KICK_ROLE         = 0x1001,  /**< 强制角色下线 */
    SS_QUERY_ONLINE      = 0x1002,  /**< 查询在线状态 */
    SS_QUERY_ONLINE_RSP  = 0x1003,  /**< 在线状态响应 */

    // ============================================================
    //  SessionServer (0x1101 ~ 0x1105)
    // ============================================================
    SES_LOAD_ROLE_REQ    = 0x1101,  /**< 加载角色社会关系数据 */
    SES_LOAD_ROLE_RSP    = 0x1102,  /**< 社会关系数据响应 */
    SES_SAVE_ROLE_REQ    = 0x1103,  /**< 保存角色社会关系数据 */
    SES_FRIEND_UPDATE    = 0x1104,  /**< 好友关系更新 */
    SES_OFFLINE_MSG_PUSH = 0x1105,  /**< 推送离线消息 */

    // ============================================================
    //  RecordServer (0x1201 ~ 0x1206)
    // ============================================================
    REC_LOAD_ROLE_REQ    = 0x1201,  /**< 从 DB 加载角色数据 */
    REC_LOAD_ROLE_RSP    = 0x1202,  /**< 角色数据加载响应 */
    REC_SAVE_ROLE_REQ    = 0x1203,  /**< 保存角色数据到 DB */
    REC_SAVE_ROLE_RSP    = 0x1204,  /**< 保存结果响应 */
    REC_LOGIN_VERIFY_REQ = 0x1205,  /**< 账号密码验证请求 */
    REC_LOGIN_VERIFY_RSP = 0x1206,  /**< 验证结果响应 */

    // ============================================================
    //  SceneServer (0x1301 ~ 0x1306)
    // ============================================================
    SCE_ROLE_ENTER_REQ   = 0x1301,  /**< 角色进入场景请求 */
    SCE_ROLE_ENTER_RSP   = 0x1302,  /**< 角色进入场景响应 */
    SCE_ROLE_LEAVE       = 0x1303,  /**< 角色离开场景通知 */
    SCE_FORWARD_TO_CLIENT= 0x1304,  /**< 向客户端转发消息（经 GatewayServer） */
    SCE_MAP_INFO_REQ     = 0x1305,  /**< 请求地图信息 */
    SCE_MAP_INFO_RSP     = 0x1306,  /**< 地图信息响应 */

    // ============================================================
    //  GatewayServer (0x1401 ~ 0x1405)
    // ============================================================
    GW_CLIENT_MSG        = 0x1401,  /**< 来自客户端的消息（转发给 SceneServer） */
    GW_SEND_TO_CLIENT    = 0x1402,  /**< SceneServer → Gateway: 发送给客户端 */
    GW_KICK_CLIENT       = 0x1403,  /**< 踢除客户端连接 */
    GW_ROLE_LOGIN_REQ    = 0x1404,  /**< Gateway → SuperServer: 发起角色登录流程 */
    GW_ROLE_LOGIN_RSP    = 0x1405,  /**< SuperServer → Gateway: 登录流程结果 */

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
    uint64_t roleID;         /**< 验证通过时的角色 ID */
    uint32_t gatewayConnID;  /**< 回显 GatewayServer 连接 ID */
};

/**
 * @brief RecordServer 加载角色数据响应
 *
 * code=0 时后续追加完整的角色二进制数据（RoleBase 序列化 + 扩展字段）。
 */
struct Msg_REC_LoadRoleRsp
{
    int32_t  code;      /**< 0=成功, -1=角色不存在 */
    uint64_t roleID;    /**< 角色 ID */
    // 后续追加 RoleBase 二进制数据 ...
};

/**
 * @brief SuperServer → SceneServer: 角色进入场景请求
 */
struct Msg_SCE_RoleEnterReq
{
    uint64_t roleID;    /**< 要进入场景的角色 ID */
    uint32_t mapID;     /**< 目标地图 ID */
    float    x, y, z;   /**< 出生点坐标 */
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
