/**
 * @file    SessionUser.h
 * @brief  SessionServer 用户对象 —— 社会关系与生命周期
 */

#pragma once
#include "../sdk/util/UserBase.h"
#include <mysql/mysql.h>
#include <cstdint>
#include <memory>
#include <vector>

struct SocialData
{
    UserID              userID = INVALID_USER_ID;
    std::vector<UserID> friends;
    std::vector<UserID> blackList;
    uint64_t            guildId = 0;
    uint32_t            teamId  = 0;
};

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
