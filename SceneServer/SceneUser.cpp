/**
 * @file    SceneUser.cpp
 * @brief  SceneUser 生命周期与 UserBase ↔ SceneEntry 同步
 */

#include "SceneUser.h"
#include "../sdk/log/Logger.h"
#include "../sdk/time/TimeUtil.h"

std::shared_ptr<SceneUser> SceneUser::create(const UserBase& base)
{
    auto user = std::shared_ptr<SceneUser>(new SceneUser(base));
    LOG_DEBUG("SceneUser::create userID=%llu", base.userID);
    return user;
}

SceneUser::SceneUser(const UserBase& base)
    : SceneEntry(base.userID)
    , IUser(base)
{
    syncFromUserBase();
}

void SceneUser::syncFromUserBase()
{
    entryId = Base().userID;
    name    = Base().name;
    level   = Base().level;
    hp      = Base().hp;
    maxHp   = Base().maxHP;
    mapId   = Base().mapID;
    posX    = Base().posX;
    posY    = Base().posY;
    posZ    = Base().posZ;
}

void SceneUser::syncToUserBase()
{
    Base().name   = name;
    Base().level  = level;
    Base().hp     = hp;
    Base().maxHP  = maxHp;
    Base().mapID  = mapId;
    Base().posX   = posX;
    Base().posY   = posY;
    Base().posZ   = posZ;
}

bool SceneUser::init()
{
    if (m_initialized) return true;

    gatewayConnId     = INVALID_CONN_ID;
    gatewayClientConn = 0;
    m_dirty           = false;
    m_lastDayStartMs  = TimeUtil::StartOfDay(TimeUtil::UnixMs());
    m_initialized     = true;
    SetState(UserState::LOADING);
    setState(SceneEntryState::OFFLINE);

    syncFromUserBase();
    LOG_DEBUG("SceneUser::init userID=%llu", GetID());
    return true;
}

bool SceneUser::onOnline()
{
    if (!m_initialized && !init()) return false;

    SetState(UserState::ONLINE);
    setState(SceneEntryState::ALIVE);
    syncFromUserBase();
    LOG_INFO("SceneUser::onOnline userID=%llu map=%u", GetID(), mapId);
    return true;
}

bool SceneUser::onOffline()
{
    SetState(UserState::OFFLINE);
    setState(SceneEntryState::OFFLINE);
    LOG_INFO("SceneUser::onOffline userID=%llu", GetID());
    return true;
}

bool SceneUser::save()
{
    if (!needSave()) return true;

    syncToUserBase();
    m_dirty = false;
    LOG_DEBUG("SceneUser::save userID=%llu (charbase sent via RecordServer)", GetID());
    return true;
}

bool SceneUser::needSave() const
{
    return m_dirty;
}

bool SceneUser::load()
{
    if (!m_initialized && !init()) return false;

    syncFromUserBase();
    m_dirty = false;
    LOG_DEBUG("SceneUser::load userID=%llu name=%s", GetID(), GetName());
    return true;
}

void SceneUser::loop(uint64_t nowMs)
{
    if (GetState() != UserState::ONLINE) return;

    syncFromUserBase();
    SceneEntry::loop(nowMs);
    OnTick(nowMs);

    const int64_t dayStart = TimeUtil::StartOfDay(static_cast<int64_t>(nowMs));
    if (m_lastDayStartMs >= 0 && dayStart != m_lastDayStartMs)
        onMidnight();
    m_lastDayStartMs = dayStart;
}

void SceneUser::onMidnight()
{
    LOG_INFO("SceneUser::onMidnight userID=%llu", GetID());
}
