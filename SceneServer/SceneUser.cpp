/**
 * @file    SceneUser.cpp
 * @brief  SceneUser 生命周期与 UserBase ↔ SceneEntry 同步
 *
 * 子系统管理器在 init/load/save/loop 中按成员依次调用，不做按类型枚举分发。
 */

#include "SceneUser.h"
#include "SceneServer.h"
#include "../sdk/log/Logger.h"
#include "../sdk/log/UserLog.h"
#include "../sdk/time/TimeUtil.h"

#include <cstdarg>

std::shared_ptr<SceneUser> SceneUser::create(const UserBase& base)
{
    auto user = std::shared_ptr<SceneUser>(new SceneUser(base));
    LOG_DEBUG("创建场景用户 userID=%llu", base.userID);
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

bool SceneUser::initManagers()
{
    return bagManager.init() &&
           itemManager.init() &&
           spellManager.init() &&
           buffManager.init() &&
           taskManager.init();
}

bool SceneUser::loadManagers()
{
    return bagManager.load() &&
           itemManager.load() &&
           spellManager.load() &&
           buffManager.load() &&
           taskManager.load();
}

bool SceneUser::saveManagers()
{
    bool ok = true;
    if (bagManager.needSave()) ok = bagManager.save() && ok;
    if (itemManager.needSave()) ok = itemManager.save() && ok;
    if (spellManager.needSave()) ok = spellManager.save() && ok;
    if (buffManager.needSave()) ok = buffManager.save() && ok;
    if (taskManager.needSave()) ok = taskManager.save() && ok;
    return ok;
}

void SceneUser::loopManagers(uint64_t nowMs)
{
    bagManager.loop(nowMs);
    itemManager.loop(nowMs);
    spellManager.loop(nowMs);
    buffManager.loop(nowMs);
    taskManager.loop(nowMs);
}

bool SceneUser::needSaveManagers() const
{
    return bagManager.needSave() ||
           itemManager.needSave() ||
           spellManager.needSave() ||
           buffManager.needSave() ||
           taskManager.needSave();
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
    if (!initManagers())
    {
        LOG_ERR("场景用户管理器初始化失败 userID=%llu", GetID());
        return false;
    }
    LOG_DEBUG("场景用户初始化完成 userID=%llu", GetID());
    return true;
}

bool SceneUser::onOnline()
{
    if (!m_initialized && !init()) return false;

    SetState(UserState::ONLINE);
    setState(SceneEntryState::ALIVE);
    syncFromUserBase();
    LOG_INFO("场景用户上线 userID=%llu map=%u", GetID(), mapId);
    return true;
}

bool SceneUser::onOffline()
{
    SetState(UserState::OFFLINE);
    setState(SceneEntryState::OFFLINE);
    LOG_INFO("场景用户下线 userID=%llu", GetID());
    return true;
}

bool SceneUser::save()
{
    if (!needSave()) return true;

    saveManagers();
    syncToUserBase();
    m_dirty = false;
    LOG_DEBUG("场景用户存档完成 userID=%llu（角色基础数据经存档服）", GetID());
    return true;
}

bool SceneUser::needSave() const
{
    return m_dirty || needSaveManagers();
}

bool SceneUser::load()
{
    if (!m_initialized && !init()) return false;

    loadManagers();
    m_dirty = false;
    LOG_DEBUG("场景用户读档完成 userID=%llu name=%s", GetID(), GetName());
    return true;
}

void SceneUser::loop(uint64_t nowMs)
{
    if (GetState() != UserState::ONLINE) return;

    syncFromUserBase();
    SceneEntry::loop(nowMs);
    OnTick(nowMs);
    loopManagers(nowMs);

    const int64_t dayStart = TimeUtil::StartOfDay(static_cast<int64_t>(nowMs));
    if (m_lastDayStartMs >= 0 && dayStart != m_lastDayStartMs)
        onMidnight();
    m_lastDayStartMs = dayStart;
}

void SceneUser::onMidnight()
{
    LOG_INFO("场景用户跨天事件 userID=%llu", GetID());
}

bool SceneUser::sendCmdToMe(uint8_t module, uint8_t sub, const char* data, uint16_t len)
{
    if (gatewayClientConn == 0)
        return false;
    return SceneServer::Instance()->SendToClient(gatewayClientConn, module, sub, data, len);
}

bool SceneUser::sendCmdToMe(uint16_t flatMsgId, const char* data, uint16_t len)
{
    return sendCmdToMe(static_cast<uint8_t>(flatMsgId >> 8),
                       static_cast<uint8_t>(flatMsgId & 0xFF),
                       data, len);
}

bool SceneUser::sendCmdToGlobal(uint16_t innerMsgId, const char* data, uint16_t len)
{
    return SceneServer::Instance()->externSender().sendToGlobalServer(innerMsgId, data, len);
}

bool SceneUser::sendCmdToZone(uint16_t innerMsgId, const char* data, uint16_t len)
{
    return SceneServer::Instance()->externSender().sendToZoneServer(innerMsgId, data, len);
}

bool SceneUser::sendCmdToLogger(uint16_t innerMsgId, const char* data, uint16_t len)
{
    return SceneServer::Instance()->externSender().sendToLoggerServer(innerMsgId, data, len);
}

bool SceneUser::sendCmdToLogin(uint16_t innerMsgId, const char* data, uint16_t len)
{
    return SceneServer::Instance()->externSender().sendToLoginServer(innerMsgId, data, len);
}

void SceneUser::info(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UserLog::info(*this, "SceneUser", "%s", buf);
}

void SceneUser::debug(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UserLog::debug(*this, "SceneUser", "%s", buf);
}

void SceneUser::warn(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UserLog::warn(*this, "SceneUser", "%s", buf);
}

void SceneUser::error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    UserLog::error(*this, "SceneUser", "%s", buf);
}
