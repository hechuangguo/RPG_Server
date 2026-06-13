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
    EntryID     npcId      = INVALID_ENTRY_ID; /**< NPC 唯一 ID */
    uint32_t    templateId = 0;       /**< 配置模板 ID */
    std::string name;                  /**< NPC 名称 */
    uint32_t    level      = 1;        /**< 等级 */
    uint32_t    hp         = 100;      /**< 当前生命 */
    uint32_t    maxHp      = 100;      /**< 生命上限 */
    uint32_t    vitality   = 100;      /**< 当前元气 */
    uint32_t    maxVitality = 100;     /**< 元气上限 */
    uint32_t    mapId      = 0;        /**< 出生地图 */
    float       posX       = 0.f;      /**< 出生坐标 X */
    float       posY       = 0.f;      /**< 出生坐标 Y */
    float       posZ       = 0.f;      /**< 出生坐标 Z */
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

    /** @brief 配置模板 ID */
    uint32_t getTemplateId() const { return templateId; }

private:
    /** @brief 仅允许工厂构造，保证初始化流程一致 */
    explicit SceneNpc(const SceneNpcDef& def);
    uint32_t templateId   = 0;  /**< 模板 ID */
    uint32_t respawnSec   = 30; /**< 复活间隔（秒） */
    uint64_t respawnAtMs  = 0;  /**< 下次复活时间戳（ms） */
    bool     initialized  = false; /**< 初始化完成标记 */
};
