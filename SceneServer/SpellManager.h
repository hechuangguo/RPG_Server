/**
 * @file    SpellManager.h
 * @brief  技能管理器（skillId → level）
 *
 * 内存技能模型（skillId → level）；每个 SceneUser 独立实例，可纳入 CharBase.binary。
 */

#pragma once
#include <cstdint>
#include <unordered_map>

/**
 * @brief 已学技能与等级
 */
class SpellManager
{
public:
    /** @brief 初始化技能表 */
    bool init();

    /** @brief 每帧驱动（预留技能 CD 刷新） */
    void loop(uint64_t nowMs);

    /** @brief 是否有待保存变更 */
    bool needSave() const;

    /** @brief 保存技能数据并清脏 */
    bool save();

    /** @brief 加载技能数据 */
    bool load();

    /** @brief 学习或升级技能 */
    bool add(uint32_t spellId, uint32_t level = 1);

    /** @brief 遗忘技能 */
    bool remove(uint32_t spellId, uint32_t unused = 0);

private:
    std::unordered_map<uint32_t, uint32_t> spellLevels; /**< skillId → level */
    bool dirty = false;                                 /**< 脏标记 */
};
