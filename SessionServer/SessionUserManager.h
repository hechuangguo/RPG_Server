/**
 * @file    SessionUserManager.h
 * @brief  SessionServer 用户管理器 —— 在线用户缓存与离线消息队列
 */

#pragma once
#include "SessionUser.h"
#include "../sdk/util/Singleton.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include "../sdk/util/RelationWireUtil.h"

/**
 * @brief 离线消息（目标用户不在线时暂存）
 */
struct OfflineMsg
{
    UserID              toId;    /**< 目标用户 ID */
    uint16_t            msgId;   /**< 协议号 */
    std::vector<char>   data;    /**< 消息体 */
};

/**
 * @brief SessionServer 用户集合管理（单例）
 */
class SessionUserManager : public LazySingleton<SessionUserManager>
{
public:
    friend class LazySingleton<SessionUserManager>;

    /** @brief 获取全局唯一实例 */
    static SessionUserManager& Instance() { return LazySingleton<SessionUserManager>::Instance(); }

    /** @brief 启动时应用 Record 预载的 Relation 行到内存缓存 */
    void applyPreloadRows(const std::vector<RelationRowData>& rows);

    /** @brief 按 userId 查找缓存用户 */
    std::shared_ptr<SessionUser> findUser(UserID userId) const;

    /** @brief 查找或创建用户并 init() */
    std::shared_ptr<SessionUser> getOrCreateUser(UserID userId);

    /** @brief 从缓存移除用户 */
    bool removeUser(UserID userId);

    /** @brief 缓存中的用户数量 */
    size_t getUserCount() const;

    /** @brief 只读遍历全部用户 */
    void forEach(const std::function<void(UserID, const std::shared_ptr<SessionUser>&)>& fn) const;

    /** @brief 向离线队列追加消息 */
    void pushOfflineMsg(UserID toId, uint16_t msgId, const char* data, uint16_t len);

    /** @brief 获取指定用户的离线消息列表（无则返回空 vector） */
    std::vector<OfflineMsg>& offlineMsgs(UserID toId);

private:
    SessionUserManager() = default;

    std::unordered_map<UserID, std::shared_ptr<SessionUser>> m_users;        /**< 在线用户缓存 */
    std::unordered_map<UserID, std::vector<OfflineMsg>>     m_offlineMsgs;   /**< 离线消息队列 */
};
