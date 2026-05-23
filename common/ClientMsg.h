#pragma once
#include <cstdint>

// ============================================================
//  客户端 <-> 服务器 消息协议号定义
//  范围约定：
//    0x0001 ~ 0x00FF : 登录/注册/角色选择
//    0x0100 ~ 0x01FF : 场景/移动
//    0x0200 ~ 0x02FF : 战斗
//    0x0300 ~ 0x03FF : 背包/物品
//    0x0400 ~ 0x04FF : 技能
//    0x0500 ~ 0x05FF : 聊天
//    0x0600 ~ 0x06FF : 社交（好友/队伍/公会）
//    0x0700 ~ 0x07FF : 任务
//    0x0F00 ~ 0x0FFF : 系统/心跳
// ============================================================

enum class ClientMsgID : uint16_t
{
    // ---------- 登录 ----------
    C2S_LOGIN_REQ        = 0x0001,  // 登录请求
    S2C_LOGIN_RSP        = 0x0002,  // 登录响应
    C2S_REGISTER_REQ     = 0x0003,  // 注册请求
    S2C_REGISTER_RSP     = 0x0004,  // 注册响应
    C2S_SELECT_ROLE_REQ  = 0x0005,  // 选择角色
    S2C_ROLE_LIST        = 0x0006,  // 角色列表
    C2S_CREATE_ROLE_REQ  = 0x0007,  // 创建角色
    S2C_CREATE_ROLE_RSP  = 0x0008,  // 创建角色响应
    S2C_ENTER_GAME       = 0x0009,  // 进入游戏

    // ---------- 场景/移动 ----------
    C2S_MOVE_REQ         = 0x0101,  // 移动请求
    S2C_MOVE_NOTIFY      = 0x0102,  // 移动广播
    S2C_ENTER_MAP        = 0x0103,  // 进入地图
    S2C_LEAVE_MAP        = 0x0104,  // 离开地图
    S2C_SPAWN_ENTITY     = 0x0105,  // 实体出现在视野
    S2C_DESPAWN_ENTITY   = 0x0106,  // 实体离开视野
    C2S_TELEPORT_REQ     = 0x0107,  // 传送请求

    // ---------- 战斗 ----------
    C2S_ATTACK_REQ       = 0x0201,  // 普攻请求
    S2C_ATTACK_NOTIFY    = 0x0202,  // 攻击结果广播
    S2C_HP_CHANGE        = 0x0203,  // 血量变化
    S2C_ENTITY_DIE       = 0x0204,  // 实体死亡

    // ---------- 背包/物品 ----------
    C2S_BAG_INFO_REQ     = 0x0301,  // 背包信息请求
    S2C_BAG_INFO_RSP     = 0x0302,  // 背包信息响应
    C2S_USE_ITEM_REQ     = 0x0303,  // 使用物品
    S2C_USE_ITEM_RSP     = 0x0304,  // 使用物品响应
    C2S_DROP_ITEM_REQ    = 0x0305,  // 丢弃物品

    // ---------- 技能 ----------
    C2S_SKILL_REQ        = 0x0401,  // 释放技能
    S2C_SKILL_NOTIFY     = 0x0402,  // 技能广播

    // ---------- 聊天 ----------
    C2S_CHAT_REQ         = 0x0501,  // 发送聊天
    S2C_CHAT_NOTIFY      = 0x0502,  // 聊天广播
    C2S_WHISPER_REQ      = 0x0503,  // 私聊
    S2C_WHISPER_NOTIFY   = 0x0504,  // 私聊通知

    // ---------- 社交 ----------
    C2S_ADD_FRIEND_REQ   = 0x0601,
    S2C_ADD_FRIEND_RSP   = 0x0602,
    S2C_FRIEND_LIST      = 0x0603,
    C2S_CREATE_TEAM_REQ  = 0x0610,
    S2C_TEAM_INFO        = 0x0611,

    // ---------- 任务 ----------
    C2S_QUEST_ACCEPT_REQ = 0x0701,
    S2C_QUEST_INFO       = 0x0702,
    C2S_QUEST_SUBMIT_REQ = 0x0703,
    S2C_QUEST_RESULT     = 0x0704,

    // ---------- 系统 ----------
    C2S_HEARTBEAT        = 0x0F01,
    S2C_HEARTBEAT        = 0x0F02,
    S2C_KICK             = 0x0F03,  // 服务器踢人
    S2C_NOTICE           = 0x0F04,  // 公告
};

// ============================================================
//  消息结构体（简化，实际项目可替换为 protobuf/flatbuffers）
// ============================================================
#pragma pack(push, 1)

struct Msg_C2S_LoginReq
{
    char account[32];
    char password[32];
};

struct Msg_S2C_LoginRsp
{
    int32_t code;       // 0=成功
    char    msg[64];
    uint64_t roleID;    // 上次角色（0=无）
};

struct Msg_C2S_MoveReq
{
    uint64_t roleID;
    float    x, y, z;
    float    dir;
    uint8_t  moveType;  // 0=走 1=跑
};

struct Msg_S2C_MoveNotify
{
    uint64_t roleID;
    float    x, y, z;
    float    dir;
    uint8_t  moveType;
};

struct Msg_C2S_Chat
{
    uint8_t  channel;   // 0=世界 1=区域 2=队伍 3=公会
    char     content[256];
};

struct Msg_S2C_Chat
{
    uint64_t fromID;
    char     fromName[32];
    uint8_t  channel;
    char     content[256];
};

struct Msg_C2S_Heartbeat
{
    uint32_t seq;
};

struct Msg_S2C_Heartbeat
{
    uint32_t seq;
    uint64_t serverTime;  // ms
};

#pragma pack(pop)
