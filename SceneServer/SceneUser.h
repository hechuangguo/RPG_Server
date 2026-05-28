/**
 * @file    SceneUser.h
 * @brief  SceneServer 在线用户 —— 继承 SceneEntry + IUser
 *
 * 各玩法子系统（包裹/道具/技能/Buff/任务）以成员管理器形式挂载；
 * 管理器自行实现 init/loop/needSave/save/load/add/remove，SceneUser 仅在生命周期中做最小调用。
 */

#pragma once
#include "SceneEntry.h"
#include "BagManager.h"
#include "BuffManager.h"
#include "ItemManager.h"
#include "SpellManager.h"
#include "TaskManager.h"
#include "../sdk/util/UserBase.h"
#include "../sdk/net/NetDefine.h"
#include <cstdint>
#include <memory>

/**
 * @brief SceneServer 内存中的在线用户
 */
class SceneUser : public SceneEntry, public IUser
{
public:
    /** @brief 基于 UserBase 构造 SceneUser 实例 */
    static std::shared_ptr<SceneUser> create(const UserBase& base);

    SceneEntryType getEntryType() const override { return SceneEntryType::USER; }

    /** @brief 初始化 SceneUser 与所有玩法管理器 */
    bool init();

    /** @brief 上线回调（恢复运行态） */
    bool onOnline();

    /** @brief 下线回调（触发必要保存） */
    bool onOffline();

    /** @brief 保存 Scene 侧角色数据与子系统状态 */
    bool save();

    /** @brief 是否有待落库改动 */
    bool needSave() const;

    /** @brief 加载 Scene 侧用户状态 */
    bool load();

    /** @brief 每帧驱动用户与子系统 */
    void loop(uint64_t nowMs) override;

    /** @brief 跨日刷新入口 */
    void onMidnight();

    /** @brief 到 Gateway 的内部连接 ID */
    ConnID getGatewayConnId() const { return gatewayConnId; }

    /** @brief 设置 Gateway 内部连接 */
    void setGatewayConnId(ConnID connId) { gatewayConnId = connId; }

    /** @brief Gateway 侧客户端连接 ID（用于回包） */
    uint32_t getGatewayClientConn() const { return gatewayClientConn; }

    /** @brief 设置 Gateway 客户端连接 ID */
    void setGatewayClientConn(uint32_t clientConn) { gatewayClientConn = clientConn; }

    /** @brief 标记 charbase 脏并同步 SceneEntry ↔ UserBase */
    void markDirty()
    {
        m_dirty = true;
        syncFromUserBase();
    }

    /** @brief 包裹子系统 */
    BagManager&   getBagManager()   { return bagManager; }

    /** @brief 道具统计子系统 */
    ItemManager&  getItemManager()  { return itemManager; }

    /** @brief 技能子系统 */
    SpellManager& getSpellManager() { return spellManager; }

    /** @brief Buff 子系统 */
    BuffManager&  getBuffManager()  { return buffManager; }

    /** @brief 任务子系统 */
    TaskManager&  getTaskManager()  { return taskManager; }

private:
    /** @brief 仅允许工厂构造，确保初始化路径统一 */
    explicit SceneUser(const UserBase& base);

    /** @brief 将 IUser 基础字段同步到 SceneEntry 展示字段 */
    void syncFromUserBase();

    /** @brief 将 SceneEntry 变更反向同步回 IUser */
    void syncToUserBase();

    /** @brief 统一驱动子管理器 loop */
    void loopManagers(uint64_t nowMs);

    /** @brief 子管理器是否有待保存状态 */
    bool needSaveManagers() const;

    /** @brief 保存所有子管理器 */
    bool saveManagers();

    /** @brief 加载所有子管理器 */
    bool loadManagers();

    /** @brief 初始化所有子管理器 */
    bool initManagers();
    ConnID   gatewayConnId     = INVALID_CONN_ID;  /**< 到 Gateway 的内部连接 */
    uint32_t gatewayClientConn = 0;                /**< Gateway 侧客户端连接 ID */
    bool     m_initialized      = false;           /**< SceneUser 初始化完成标记 */
    bool     m_dirty            = false;           /**< UserBase/charbase 脏标记 */
    int64_t  m_lastDayStartMs   = -1;             /**< 上次跨日基准时间戳 */
    BagManager   bagManager;    /**< 包裹系统 */
    ItemManager  itemManager;   /**< 道具统计系统 */
    SpellManager spellManager;  /**< 技能系统 */
    BuffManager  buffManager;   /**< Buff 系统 */
    TaskManager  taskManager;   /**< 任务系统 */
};
