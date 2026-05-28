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

    EntryID getEntryId() const { return entryId; }
    const std::string& getName() const { return name; }
    uint32_t getLevel() const { return level; }
    uint32_t getHp() const { return hp; }
    uint32_t getMaxHp() const { return maxHp; }
    uint32_t getVitality() const { return vitality; }
    uint32_t getMaxVitality() const { return maxVitality; }
    uint32_t getMapId() const { return mapId; }
    float getPosX() const { return posX; }
    float getPosY() const { return posY; }
    float getPosZ() const { return posZ; }
    SceneEntryState getState() const { return state; }

    void setName(const std::string& n) { name = n; }
    void setLevel(uint32_t lv) { level = lv; }
    void setHp(uint32_t v) { hp = v > maxHp ? maxHp : v; }
    void setMaxHp(uint32_t v) { maxHp = v; if (hp > maxHp) hp = maxHp; }
    void setVitality(uint32_t v) { vitality = v > maxVitality ? maxVitality : v; }
    void setMaxVitality(uint32_t v) { maxVitality = v; if (vitality > maxVitality) vitality = maxVitality; }
    void setMapId(uint32_t id) { mapId = id; }
    void setPos(float x, float y, float z) { posX = x; posY = y; posZ = z; }
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
