/**
 * @file    SceneNpc.h
 * @brief  场景 NPC —— 继承 SceneEntry，提供创建、循环、死亡等接口
 */

#pragma once
#include "SceneEntry.h"
#include <cstdint>
#include <memory>
#include <string>

/** @brief 创建 NPC 时的初始数据 */
struct SceneNpcDef
{
    EntryID     npcId      = INVALID_ENTRY_ID;
    uint32_t    templateId = 0;       /**< 配置模板 ID */
    std::string name;
    uint32_t    level      = 1;
    uint32_t    hp         = 100;
    uint32_t    maxHp      = 100;
    uint32_t    vitality   = 100;
    uint32_t    maxVitality = 100;
    uint32_t    mapId      = 0;
    float       posX       = 0.f;
    float       posY       = 0.f;
    float       posZ       = 0.f;
    uint32_t    respawnSec = 30;        /**< 死亡后复活秒数 */
};

/**
 * @brief 场景 NPC 对象
 */
class SceneNpc : public SceneEntry
{
public:
    /** @brief 工厂创建 */
    static std::shared_ptr<SceneNpc> create(const SceneNpcDef& def);

    /** @brief 初始化运行时状态 */
    bool init();

    SceneEntryType getEntryType() const override { return SceneEntryType::NPC; }

    /** @brief 每帧驱动（含复活检测） */
    void loop(uint64_t nowMs) override;

    /** @brief NPC 死亡处理 */
    void onDeath();

    /** @brief 是否已死亡 */
    bool isDead() const { return getState() == SceneEntryState::DEAD; }

    uint32_t getTemplateId() const { return templateId; }

private:
    explicit SceneNpc(const SceneNpcDef& def);

    uint32_t templateId   = 0;
    uint32_t respawnSec   = 30;
    uint64_t respawnAtMs  = 0;
    bool     initialized  = false;
};
