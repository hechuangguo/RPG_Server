/**
 * @file    SceneUser.h
 * @brief  SceneServer 在线用户 —— 继承 SceneEntry + IUser
 */

#pragma once
#include "SceneEntry.h"
#include "../sdk/util/UserBase.h"
#include "../sdk/net/NetDefine.h"
#include <cstdint>
#include <memory>

/**
 * @brief SceneServer 内存中的在线用户
 *
 * 与 SceneNpc 同级继承 SceneEntry，同时保留 IUser 接口与 UserBase 持久化。
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

    void markDirty()
    {
        m_dirty = true;
        syncFromUserBase();
    }

private:
    explicit SceneUser(const UserBase& base);

    void syncFromUserBase();
    void syncToUserBase();

    ConnID   gatewayConnId     = INVALID_CONN_ID;
    uint32_t gatewayClientConn = 0;
    bool     m_initialized      = false;
    bool     m_dirty            = false;
    int64_t  m_lastDayStartMs   = -1;
};
