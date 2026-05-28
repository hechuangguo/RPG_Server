/**
 * @file    SessionUser.h
 * @brief  SessionServer 用户对象 —— 社会关系与生命周期
 *
 * 直连 MySQL 读写 Relation；save/load 在 onOffline 等时机由 SessionServer 调用。
 */

#pragma once
#include "../sdk/util/UserBase.h"
#include <mysql/mysql.h>
#include <cstdint>
#include <memory>
#include <vector>

/** @brief 用户社交数据（内存态，与 Relation 表字段对应） */
struct SocialData
{
    UserID              userID = INVALID_USER_ID; /**< 所属用户 ID */
    std::vector<UserID> friends;    /**< 好友 user_id 列表 */
    std::vector<UserID> blackList;  /**< 黑名单 */
    uint64_t            guildId = 0;              /**< 公会 ID（0 表示无公会） */
    uint32_t            teamId  = 0;              /**< 当前队伍 ID（0 表示无队伍） */
    std::vector<uint8_t> binary;   /**< 社交扩展二进制（对应 Relation.binary） */
};

/**
 * @brief Session 进程内用户实例
 */
class SessionUser : public IUser
{
public:
    /** @brief 基于 UserBase 创建并返回 SessionUser 智能指针 */
    static std::shared_ptr<SessionUser> create(const UserBase& base);

    /** @brief 初始化社交缓存与脏标记状态 */
    bool init();

    /** @brief 用户上线钩子（准备会话态资源） */
    bool onOnline();

    /** @brief 用户下线钩子（触发必要保存） */
    bool onOffline();

    /** @brief 将社交数据写回 Session 存储 */
    bool save(MYSQL* db);

    /** @brief 是否存在待落库改动 */
    bool needSave() const;

    /** @brief 从 Session 存储加载社交数据 */
    bool load(MYSQL* db);

    /** @brief 用 Relation 查询行字段填充 m_social（不 INSERT） */
    void applyRelationFromDbRow(const char* friendsJson, const char* blacklistJson,
                                const char* guildIdStr, const char* teamIdStr,
                                const char* binaryPtr, unsigned long binaryLen);

    /** @brief 帧循环更新（跨日判断等） */
    void loop(uint64_t nowMs);

    /** @brief 跨日刷新入口 */
    void onMidnight();

    /** @brief 可写社交数据引用 */
    SocialData& social() { return m_social; }

    /** @brief 只读社交数据引用 */
    const SocialData& social() const { return m_social; }

    /** @brief 标记社交数据待落库 */
    void markDirty() { m_dirty = true; }

private:
    /** @brief 仅允许通过 create 构造，确保初始化流程一致 */
    explicit SessionUser(const UserBase& base);
    SocialData m_social;                               /**< 社交关系缓存 */
    bool       m_initialized    = false;               /**< 是否已初始化 */
    bool       m_dirty          = false;               /**< 是否需要保存 */
    int64_t    m_lastDayStartMs = -1;                  /**< 上次跨日基准时间戳（ms） */
};
