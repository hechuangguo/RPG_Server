/**
 * @file    SpellManager.h
 * @brief  技能管理器（skillId → level）
 *
 * 对应 DB 表 t_skill 的简化内存模型；每个 SceneUser 独立实例。
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
    bool init();
    void loop(uint64_t nowMs);
    bool needSave() const;
    bool save();
    bool load();

    /** @brief 学习或升级技能 */
    bool add(uint32_t spellId, uint32_t level = 1);

    /** @brief 遗忘技能 */
    bool remove(uint32_t spellId, uint32_t unused = 0);

private:
    std::unordered_map<uint32_t, uint32_t> spellLevels; /**< skillId → level */
    bool dirty = false;
};
