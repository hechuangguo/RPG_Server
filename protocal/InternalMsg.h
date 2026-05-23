#pragma once
#include <cstdint>

// ============================================================
//  服务器内部消息协议号定义
//  范围约定：
//    0x1000 ~ 0x10FF : SuperServer 相关
//    0x1100 ~ 0x11FF : Session 相关
//    0x1200 ~ 0x12FF : Record 相关
//    0x1300 ~ 0x13FF : Scene 相关
//    0x1400 ~ 0x14FF : Gateway 相关
//    0x1500 ~ 0x15FF : AOI 相关
//    0x1600 ~ 0x16FF : Logger 相关
//    0x1700 ~ 0x17FF : Global 相关
//    0x1800 ~ 0x18FF : Zone 相关
//    0x1F00 ~ 0x1FFF : 内部心跳/注册
// ============================================================

enum class InternalMsgID : uint16_t
{
    // ---------- 服务器注册/心跳 ----------
    S2S_REGISTER_REQ     = 0x1F01,  // 子服务器向 SuperServer 注册
    S2S_REGISTER_RSP     = 0x1F02,
    S2S_HEARTBEAT        = 0x1F03,
    S2S_HEARTBEAT_ACK    = 0x1F04,

    // ---------- SuperServer ----------
    SS_KICK_ROLE         = 0x1001,  // 踢下线
    SS_QUERY_ONLINE      = 0x1002,
    SS_QUERY_ONLINE_RSP  = 0x1003,

    // ---------- Session ----------
    SES_LOAD_ROLE_REQ    = 0x1101,  // 加载社会关系数据
    SES_LOAD_ROLE_RSP    = 0x1102,
    SES_SAVE_ROLE_REQ    = 0x1103,
    SES_FRIEND_UPDATE    = 0x1104,
    SES_OFFLINE_MSG_PUSH = 0x1105,

    // ---------- Record ----------
    REC_LOAD_ROLE_REQ    = 0x1201,  // 从 DB 加载角色数据
    REC_LOAD_ROLE_RSP    = 0x1202,
    REC_SAVE_ROLE_REQ    = 0x1203,
    REC_SAVE_ROLE_RSP    = 0x1204,
    REC_LOGIN_VERIFY_REQ = 0x1205,  // 账号密码验证
    REC_LOGIN_VERIFY_RSP = 0x1206,

    // ---------- Scene ----------
    SCE_ROLE_ENTER_REQ   = 0x1301,  // 角色进入场景
    SCE_ROLE_ENTER_RSP   = 0x1302,
    SCE_ROLE_LEAVE       = 0x1303,  // 角色离开场景
    SCE_FORWARD_TO_CLIENT= 0x1304,  // 向客户端转发消息
    SCE_MAP_INFO_REQ     = 0x1305,
    SCE_MAP_INFO_RSP     = 0x1306,

    // ---------- Gateway ----------
    GW_CLIENT_MSG        = 0x1401,  // 来自客户端的原始消息
    GW_SEND_TO_CLIENT    = 0x1402,  // 发送给客户端的消息
    GW_KICK_CLIENT       = 0x1403,  // 踢客户端
    GW_ROLE_LOGIN_REQ    = 0x1404,  // 发起登录流程
    GW_ROLE_LOGIN_RSP    = 0x1405,

    // ---------- AOI ----------
    AOI_ENTER_REQ        = 0x1501,  // 进入 AOI
    AOI_LEAVE_REQ        = 0x1502,  // 离开 AOI
    AOI_MOVE_REQ         = 0x1503,  // 移动更新
    AOI_VIEW_NOTIFY      = 0x1504,  // 视野变化通知

    // ---------- Logger ----------
    LOG_WRITE_REQ        = 0x1601,  // 写日志请求

    // ---------- Global ----------
    GLB_DATA_SYNC        = 0x1701,  // 全区数据同步
    GLB_RANK_UPDATE      = 0x1702,  // 排行榜更新

    // ---------- Zone ----------
    ZONE_CROSS_REQ       = 0x1801,  // 跨区请求
    ZONE_CROSS_RSP       = 0x1802,
    ZONE_FORWARD         = 0x1803,  // 跨区转发
};

// ============================================================
//  内部消息结构体
// ============================================================
#pragma pack(push, 1)

struct Msg_S2S_Register
{
    uint8_t  serverType;  // 服务器类型枚举
    uint32_t serverID;
    char     ip[32];
    uint16_t port;
};

struct Msg_S2S_Heartbeat
{
    uint32_t seq;
    uint64_t timestamp;
};

struct Msg_REC_LoginVerifyReq
{
    char     account[32];
    char     password[32];
    uint32_t gatewayConnID;  // 来自哪个网关连接
};

struct Msg_REC_LoginVerifyRsp
{
    int32_t  code;
    uint64_t roleID;
    uint32_t gatewayConnID;
};

struct Msg_REC_LoadRoleRsp
{
    int32_t  code;
    uint64_t roleID;
    // 后续追加完整角色二进制数据
};

struct Msg_SCE_RoleEnterReq
{
    uint64_t roleID;
    uint32_t mapID;
    float    x, y, z;
};

struct Msg_GW_ClientMsg
{
    uint32_t clientConnID;
    uint16_t msgID;
    uint16_t dataLen;
    // 后跟 dataLen 字节的消息体
};

struct Msg_AOI_Move
{
    uint64_t entityID;
    uint32_t mapID;
    float    x, y, z;
    float    dir;
};

#pragma pack(pop)
