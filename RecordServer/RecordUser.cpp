/**
 * @file    RecordUser.cpp
 * @brief  RecordUser 创建 / 读档 / 存档简单实现
 */

#include "RecordUser.h"
#include "../sdk/log/Logger.h"
#include "../sdk/time/TimeUtil.h"

std::shared_ptr<RecordUser> RecordUser::create(const UserBase& base)
{
    auto user = std::shared_ptr<RecordUser>(new RecordUser(base));
    LOG_DEBUG("创建存档用户对象 userID=%llu", base.userID);
    return user;
}

RecordUser::RecordUser(const UserBase& base)
    : IUser(base)
{
    m_record.base = base;
}

bool RecordUser::init()
{
    if (m_initialized) return true;

    m_record.base.userID = GetID();
    m_record.dirty     = false;
    m_initialized        = true;
    SetState(UserState::LOADING);

    LOG_DEBUG("存档用户初始化完成 userID=%llu", GetID());
    return true;
}

bool RecordUser::save()
{
    if (!m_initialized && !init()) return false;

    m_record.base   = Base();
    m_record.lastSaveTime = static_cast<uint64_t>(TimeUtil::UnixMs());
    m_record.dirty        = false;
    SetState(UserState::OFFLINE);

    LOG_DEBUG("存档用户保存完成 userID=%llu level=%u", GetID(), m_record.base.level);
    return true;
}

bool RecordUser::load()
{
    if (!m_initialized && !init()) return false;

    Base() = m_record.base;
    m_record.dirty = false;
    SetState(UserState::OFFLINE);

    LOG_DEBUG("存档用户读档完成 userID=%llu name=%s", GetID(), GetName());
    return true;
}
