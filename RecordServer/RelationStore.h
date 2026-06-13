/**
 * @file    RelationStore.h
 * @brief   Relation 表读写（仅 RecordServer 直连 MySQL）
 *
 * SessionServer 经 REC_RELATION_* 协议访问；本类封装 SQL 与线格式编解码。
 */

#pragma once

#include "../sdk/util/RelationWireUtil.h"

#include <cstdint>
#include <string>
#include <vector>

#include <mysql/mysql.h>

using RelationRow = RelationRowData;

/**
 * @brief Relation 表访问层
 */
class RelationStore
{
public:
    explicit RelationStore(MYSQL* db);

    /**
     * @brief 全表预载（Session 启动缓存）
     * @param out 输出所有行
     * @return 成功 true
     */
    bool preloadAll(std::vector<RelationRow>& out) const;

    /**
     * @brief 按 userID 加载一行；不存在则插入空行后返回默认值
     * @param userID 用户 ID
     * @param out    输出行
     * @return 成功 true
     */
    bool loadOne(uint64_t userID, RelationRow& out) const;

    /**
     * @brief UPSERT 一行 Relation
     * @param row 待写入数据
     * @return 成功 true
     */
    bool saveOne(const RelationRow& row) const;

    /**
     * @brief 将多行编码为 REC_RELATION_PRELOAD_RSP body（含 Msg_REC_RelationPreloadRsp 头）
     * @param code  结果码
     * @param rows  行数据
     * @param out   完整发送缓冲区
     */
    static void encodePreloadRsp(int32_t code, const std::vector<RelationRow>& rows,
                                 std::vector<char>& out);

    /**
     * @brief 将单行编码为 REC_RELATION_LOAD_RSP body（含 Msg_REC_RelationLoadRsp 头）
     */
    static void encodeLoadRsp(int32_t code, uint64_t userID, const RelationRow* row,
                              std::vector<char>& out);

    /**
     * @brief 从 REC_RELATION_SAVE_REQ 变长 body 解析一行
     * @return 成功 true
     */
    static bool decodeSaveReq(const char* data, uint16_t len, RelationRow& out);

    /**
     * @brief 从预载/加载响应 body 解析所有行（跳过响应头）
     */
    static bool decodeRows(const char* data, uint16_t len, size_t headerSize,
                           std::vector<RelationRow>& out);

private:
    MYSQL* m_db; /**< RecordServer 持有的 MySQL 连接 */
};
