/**
 * @file    SceneEntry.h
 * @brief  场景物件基类 —— 用户与 NPC 的公共属性与接口
 */

#pragma once
#include <cstdint>
#include <string>

/** @brief 场景物件唯一 ID（玩家 userID / NPC npcID） */
using EntryID = uint64_t;

/** @brief 无效物件 ID */
constexpr EntryID INVALID_ENTRY_ID = 0;

/** @brief 场景物件类型 */
enum class SceneEntryType : uint8_t
{
    USER = 1,  /**< 在线玩家 */
    NPC  = 2,  /**< 地图 NPC */
};

/** @brief 场景物件生存状态 */
enum class SceneEntryState : uint8_t
{
    OFFLINE = 0,  /**< 离线或未激活 */
    ALIVE   = 1,  /**< 存活 */
    DEAD    = 2,  /**< 已死亡 */
};

/**
 * @brief 场景物件基类
 *
 * 提供名字、等级、血量、元气等基础数据；SceneUser / SceneNpc 同级继承。
 */
class SceneEntry
{
public:
    virtual ~SceneEntry() = default;

    /** @brief 物件类型（USER / NPC） */
    virtual SceneEntryType getEntryType() const = 0;

    /** @brief 实体唯一 ID */
    EntryID getEntryId() const { return entryId; }

    /** @brief 显示名称 */
    const std::string& getName() const { return name; }

    /** @brief 等级 */
    uint32_t getLevel() const { return level; }

    /** @brief 当前生命值 */
    uint32_t getHp() const { return hp; }

    /** @brief 生命上限 */
    uint32_t getMaxHp() const { return maxHp; }

    /** @brief 当前元气 */
    uint32_t getVitality() const { return vitality; }

    /** @brief 元气上限 */
    uint32_t getMaxVitality() const { return maxVitality; }

    /** @brief 所在地图 ID */
    uint32_t getMapId() const { return mapId; }

    /** @brief 世界坐标 X */
    float getPosX() const { return posX; }

    /** @brief 世界坐标 Y */
    float getPosY() const { return posY; }

    /** @brief 世界坐标 Z */
    float getPosZ() const { return posZ; }

    /** @brief 生存状态 */
    SceneEntryState getState() const { return state; }

    /** @brief 设置显示名称 */
    void setName(const std::string& n) { name = n; }

    /** @brief 设置等级 */
    void setLevel(uint32_t lv) { level = lv; }

    /** @brief 设置当前 HP（不超过 maxHp） */
    void setHp(uint32_t v) { hp = v > maxHp ? maxHp : v; }

    /** @brief 设置 HP 上限并钳制当前 HP */
    void setMaxHp(uint32_t v) { maxHp = v; if (hp > maxHp) hp = maxHp; }

    /** @brief 设置当前元气（不超过 maxVitality） */
    void setVitality(uint32_t v) { vitality = v > maxVitality ? maxVitality : v; }

    /** @brief 设置元气上限并钳制当前元气 */
    void setMaxVitality(uint32_t v) { maxVitality = v; if (vitality > maxVitality) vitality = maxVitality; }

    /** @brief 设置所在地图 */
    void setMapId(uint32_t id) { mapId = id; }

    /** @brief 设置世界坐标 */
    void setPos(float x, float y, float z) { posX = x; posY = y; posZ = z; }

    /** @brief 设置生存状态 */
    void setState(SceneEntryState s) { state = s; }

    /** @brief 是否存活 */
    bool isAlive() const { return state == SceneEntryState::ALIVE; }

    /** @brief 每帧驱动（子类可覆写） */
    virtual void loop(uint64_t nowMs);

protected:
    /** @brief 构造基础场景实体（子类传入唯一 entryId） */
    explicit SceneEntry(EntryID id);
    EntryID          entryId     = INVALID_ENTRY_ID;     /**< 实体唯一 ID */
    std::string      name;                               /**< 名称（玩家名/NPC 名） */
    uint32_t         level       = 1;                    /**< 等级 */
    uint32_t         hp          = 100;                  /**< 当前生命值 */
    uint32_t         maxHp       = 100;                  /**< 生命上限 */
    uint32_t         vitality    = 100;  /**< 元气（当前） */
    uint32_t         maxVitality = 100;  /**< 元气上限 */
    uint32_t         mapId       = 0;                    /**< 当前地图 ID */
    float            posX        = 0.f;                  /**< 世界坐标 X */
    float            posY        = 0.f;                  /**< 世界坐标 Y */
    float            posZ        = 0.f;                  /**< 世界坐标 Z */
    SceneEntryState  state       = SceneEntryState::OFFLINE; /**< 当前生存状态 */
};
