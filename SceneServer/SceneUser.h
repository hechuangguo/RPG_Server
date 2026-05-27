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
    static std::shared_ptr<SceneUser> create(const UserBase& base);

    SceneEntryType getEntryType() const override { return SceneEntryType::USER; }

    bool init();
    bool onOnline();
    bool onOffline();
    bool save();
    bool needSave() const;
    bool load();
    void loop(uint64_t nowMs) override;
    void onMidnight();

    ConnID getGatewayConnId() const { return gatewayConnId; }
    void setGatewayConnId(ConnID connId) { gatewayConnId = connId; }

    uint32_t getGatewayClientConn() const { return gatewayClientConn; }
    void setGatewayClientConn(uint32_t clientConn) { gatewayClientConn = clientConn; }

    /** @brief 标记 charbase 脏并同步 SceneEntry ↔ UserBase */
    void markDirty()
    {
        m_dirty = true;
        syncFromUserBase();
    }

    BagManager&   getBagManager()   { return bagManager; }
    ItemManager&  getItemManager()  { return itemManager; }
    SpellManager& getSpellManager() { return spellManager; }
    BuffManager&  getBuffManager()  { return buffManager; }
    TaskManager&  getTaskManager()  { return taskManager; }

private:
    explicit SceneUser(const UserBase& base);

    void syncFromUserBase();
    void syncToUserBase();
    void loopManagers(uint64_t nowMs);
    bool needSaveManagers() const;
    bool saveManagers();
    bool loadManagers();
    bool initManagers();

    ConnID   gatewayConnId     = INVALID_CONN_ID;  /**< 到 Gateway 的内部连接 */
    uint32_t gatewayClientConn = 0;                /**< Gateway 侧客户端连接 ID */
    bool     m_initialized      = false;
    bool     m_dirty            = false;           /**< UserBase/charbase 脏标记 */
    int64_t  m_lastDayStartMs   = -1;

    BagManager   bagManager;
    ItemManager  itemManager;
    SpellManager spellManager;
    BuffManager  buffManager;
    TaskManager  taskManager;
};
