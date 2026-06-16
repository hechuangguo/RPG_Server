/**
 * @file    SceneNpc.cpp
 * @brief  SceneNpc 创建、循环、死亡与复活
 */

#include "SceneNpc.h"
#include "SceneServer.h"
#include "../sdk/log/Logger.h"
std::shared_ptr<SceneNpc> SceneNpc::create(const SceneNpcDef& def)
{
    if (def.npcId == INVALID_ENTRY_ID)
        return nullptr;

    auto npc = std::shared_ptr<SceneNpc>(new SceneNpc(def));
    LOG_DEBUG("创建场景怪物: npcId=%llu template=%u", def.npcId, def.templateId);
    return npc;
}

SceneNpc::SceneNpc(const SceneNpcDef& def)
    : SceneEntry(def.npcId)
    , templateId(def.templateId)
    , respawnSec(def.respawnSec > 0 ? def.respawnSec : 30)
{
    name         = def.name;
    level        = def.level;
    hp           = def.hp;
    maxHp        = def.maxHp > 0 ? def.maxHp : def.hp;
    vitality     = def.vitality;
    maxVitality  = def.maxVitality > 0 ? def.maxVitality : def.vitality;
    mapId        = def.mapId;
    posX         = def.posX;
    posY         = def.posY;
    posZ         = def.posZ;
}

bool SceneNpc::init()
{
    if (initialized) return true;

    if (hp == 0)
        setState(SceneEntryState::DEAD);
    else
        setState(SceneEntryState::ALIVE);

    respawnAtMs  = 0;
    initialized  = true;
    LOG_DEBUG("场景怪物初始化完成: npcId=%llu name=%s map=%u",
              getEntryId(), getName().c_str(), getMapId());
    return true;
}

void SceneNpc::loop(uint64_t nowMs)
{
    if (!initialized) return;

    if (getState() == SceneEntryState::DEAD)
    {
        if (respawnAtMs == 0 && respawnSec > 0)
            respawnAtMs = nowMs + static_cast<uint64_t>(respawnSec) * 1000;

        if (respawnAtMs > 0 && nowMs >= respawnAtMs)
        {
            setHp(maxHp);
            setVitality(maxVitality);
            setState(SceneEntryState::ALIVE);
            respawnAtMs = 0;
            LOG_INFO("场景怪物复活: npcId=%llu map=%u", getEntryId(), getMapId());
            if (auto* srv = SceneServer::Instance())
                srv->notifyNpcEnterAoi(*this);
        }
        return;
    }

    if (!isAlive()) return;
    // TODO: AI、巡逻、仇恨等
}

void SceneNpc::onDeath()
{
    if (getState() == SceneEntryState::DEAD) return;

    setHp(0);
    setState(SceneEntryState::DEAD);
    respawnAtMs = 0;
    LOG_INFO("场景怪物死亡: npcId=%llu name=%s", getEntryId(), getName().c_str());
    if (auto* srv = SceneServer::Instance())
        srv->notifyNpcLeaveAoi(getEntryId());
}
