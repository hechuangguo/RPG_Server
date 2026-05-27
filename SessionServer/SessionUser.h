/**
 * @file    SessionUser.h
 * @brief  SessionServer 用户对象 —— 社会关系与生命周期
 *
 * 直连 MySQL 读写 t_relation；save/load 在 onOffline 等时机由 SessionServer 调用。
 */

#pragma once
#include "../sdk/util/UserBase.h"
#include <mysql/mysql.h>
#include <cstdint>
#include <memory>
#include <vector>

/** @brief 用户社交数据（内存态，与 t_relation 字段对应） */
struct SocialData
{
    UserID              userID = INVALID_USER_ID;
    std::vector<UserID> friends;    /**< 好友 user_id 列表 */
    std::vector<UserID> blackList;  /**< 黑名单 */
    uint64_t            guildId = 0;
    uint32_t            teamId  = 0;
};

/**
 * @brief Session 进程内用户实例
 */
class SessionUser : public IUser
{
public:
    static std::shared_ptr<SessionUser> create(const UserBase& base);

    bool init();
    bool onOnline();
    bool onOffline();
    bool save(MYSQL* db);
    bool needSave() const;
    bool load(MYSQL* db);
    void loop(uint64_t nowMs);
    void onMidnight();

    SocialData& social() { return m_social; }
    const SocialData& social() const { return m_social; }

    void markDirty() { m_dirty = true; }

private:
    explicit SessionUser(const UserBase& base);

    SocialData m_social;
    bool       m_initialized    = false;
    bool       m_dirty          = false;
    int64_t    m_lastDayStartMs = -1;
};
