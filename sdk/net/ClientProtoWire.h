/**
 * @file    ClientProtoWire.h
 * @brief  客户端 Protobuf 编解码与地图 spawn 辅助
 *
 * body 为 proto3 二进制；6 字节 MsgHeader 路由不变（见 Common/NetDefine.h）。
 */

#pragma once

#include <cstdint>
#include <string>

#include "LoginCommon.pb.h"
#include "LoginMsg.pb.h"
#include "MapDataCommon.pb.h"
#include "MapDataMsg.pb.h"
#include "ZoneCommon.pb.h"
#include "ZoneMsg.pb.h"
#include "ChatCommon.pb.h"
#include "ChatMsg.pb.h"
#include "SystemCommon.pb.h"
#include "SystemMsg.pb.h"
#include "NpcCommon.pb.h"
#include "NpcMsg.pb.h"

/** @brief Protobuf body 最大长度（Gateway Validator maxLen） */
constexpr uint16_t CLIENT_PROTO_MAX_BODY = 65535;

/** @brief 聊天内容 UTF-8 最大字节数（Validator 约束） */
constexpr uint32_t MAX_CHAT_CONTENT_BYTES = 256;

/** @brief 系统公告最大字节数 */
constexpr uint32_t MAX_NOTICE_CONTENT_BYTES = 512;

/** @brief NPC 对话选项上限 */
constexpr uint32_t MAX_NPC_TALK_OPTIONS = 4;

/** @brief 区列表 gameType 过滤：全部 */
constexpr uint32_t ZONE_LIST_ALL_GAME_TYPES = 0xFF;

/** @brief 区列表响应最大条目数（LoginServer serverlist.xml） */
constexpr uint16_t MAX_ZONE_LIST_ENTRIES = 64;

/** @brief 解析 Protobuf body */
template<typename T>
bool parseProto(const char* body, uint16_t len, T& out)
{
    return out.ParseFromArray(body, static_cast<int>(len));
}

/** @brief 序列化 Protobuf body */
template<typename T>
bool serializeProto(const T& msg, std::string& out)
{
    return msg.SerializeToString(&out);
}

/** @brief legacy entityType(0=player) → proto EntityType */
rpg::mapdata::EntityType toProtoEntityType(uint8_t legacyType);

/** @brief 填充 S2CSpawnEntity */
void fillProtoSpawnEntity(uint64_t entityId, const std::string& name, uint32_t level,
                          float x, float y, float z, float dir, uint8_t legacyEntityType,
                          uint32_t modelId, uint32_t animState,
                          rpg::mapdata::S2CSpawnEntity& out);

inline bool parseMoveReq(const char* data, uint16_t len, rpg::mapdata::C2SMoveReq& out)
{
    return parseProto(data, len, out);
}

inline bool serializeMoveNotify(const rpg::mapdata::S2CMoveNotify& msg, std::string& out)
{
    return serializeProto(msg, out);
}

inline bool serializeSpawnEntity(const rpg::mapdata::S2CSpawnEntity& msg, std::string& out)
{
    return serializeProto(msg, out);
}

inline bool serializeDespawnEntity(const rpg::mapdata::S2CDespawnEntity& msg, std::string& out)
{
    return serializeProto(msg, out);
}
