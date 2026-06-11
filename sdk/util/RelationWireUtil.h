/**
 * @file    RelationWireUtil.h
 * @brief   Relation 线格式编解码（Session ↔ Record 共用）
 */

#pragma once

#include "../../protocal/InternalMsg.h"

#include <cstring>
#include <string>
#include <vector>

/** @brief Relation 内存行（与 DB / 线格式对应） */
struct RelationRowData
{
    uint64_t             userID = 0;
    std::string          friendsJson;
    std::string          blacklistJson;
    uint64_t             guildId = 0;
    uint32_t             teamId  = 0;
    std::vector<uint8_t> binary;
};

namespace RelationWireUtil {

inline constexpr size_t kRowHeaderSize = sizeof(RelationWireRowHeader);

/** @brief 将一行追加到变长 body 缓冲区 */
inline void appendRow(const RelationRowData& row, std::vector<char>& out)
{
    RelationWireRowHeader hdr{};
    hdr.userID       = row.userID;
    hdr.friendsLen   = static_cast<uint32_t>(row.friendsJson.size());
    hdr.blacklistLen = static_cast<uint32_t>(row.blacklistJson.size());
    hdr.guildId      = row.guildId;
    hdr.teamId       = row.teamId;
    hdr.binaryLen    = static_cast<uint32_t>(row.binary.size());

    const size_t base = out.size();
    out.resize(base + kRowHeaderSize + hdr.friendsLen + hdr.blacklistLen + hdr.binaryLen);
    char* p = out.data() + base;
    memcpy(p, &hdr, kRowHeaderSize);
    p += kRowHeaderSize;
    if (hdr.friendsLen > 0)
    {
        memcpy(p, row.friendsJson.data(), hdr.friendsLen);
        p += hdr.friendsLen;
    }
    if (hdr.blacklistLen > 0)
    {
        memcpy(p, row.blacklistJson.data(), hdr.blacklistLen);
        p += hdr.blacklistLen;
    }
    if (hdr.binaryLen > 0)
        memcpy(p, row.binary.data(), hdr.binaryLen);
}

/** @brief 从变长 body 解析一行（更新 offset） */
inline bool parseRow(const char* data, uint16_t len, size_t& offset, RelationRowData& row)
{
    if (offset + kRowHeaderSize > len)
        return false;
    RelationWireRowHeader hdr{};
    memcpy(&hdr, data + offset, kRowHeaderSize);
    offset += kRowHeaderSize;

    const size_t need = static_cast<size_t>(hdr.friendsLen) + hdr.blacklistLen + hdr.binaryLen;
    if (offset + need > len)
        return false;

    row.userID = hdr.userID;
    row.guildId = hdr.guildId;
    row.teamId  = hdr.teamId;
    row.friendsJson.assign(data + offset, hdr.friendsLen);
    offset += hdr.friendsLen;
    row.blacklistJson.assign(data + offset, hdr.blacklistLen);
    offset += hdr.blacklistLen;
    row.binary.assign(reinterpret_cast<const uint8_t*>(data + offset),
                      reinterpret_cast<const uint8_t*>(data + offset) + hdr.binaryLen);
    offset += hdr.binaryLen;
    return true;
}

/** @brief 解析响应头之后的全部行 */
inline bool parseAllRows(const char* data, uint16_t len, size_t headerSize,
                         std::vector<RelationRowData>& out)
{
    out.clear();
    size_t offset = headerSize;
    while (offset < len)
    {
        RelationRowData row;
        if (!parseRow(data, len, offset, row))
            return false;
        out.push_back(std::move(row));
    }
    return true;
}

} // namespace RelationWireUtil
