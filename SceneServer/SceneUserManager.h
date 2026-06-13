/**
 * @file    SceneUserManager.h
 * @brief  SceneServer 在线用户管理器
 */

#pragma once
#include "SceneUser.h"
#include "../sdk/util/Singleton.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>

/**
 * @brief SceneServer 在线用户集合（userId → SceneUser，单例）
 */
class SceneUserManager : public LazySingleton<SceneUserManager>
{
public:
    friend class LazySingleton<SceneUserManager>;

    /** @brief 获取全局唯一实例 */
    static SceneUserManager& Instance() { return LazySingleton<SceneUserManager>::Instance(); }

    /** @brief 按 userId 查找在线用户 */
    std::shared_ptr<SceneUser> findUser(UserID userId) const;

    /** @brief 按 Gateway 客户端 connId 反查用户 */
    std::shared_ptr<SceneUser> findUserByClientConn(uint32_t clientConnId) const;

    /** @brief 注册在线用户 */
    bool addUser(UserID userId, std::shared_ptr<SceneUser> user);

    /** @brief 移除在线用户 */
    bool removeUser(UserID userId);

    /** @brief 当前在线人数 */
    size_t getUserCount() const;

    /** @brief 只读遍历全部用户 */
    void forEach(const std::function<void(UserID, const std::shared_ptr<SceneUser>&)>& fn) const;

    /** @brief 遍历并提供可写 SceneUser 引用 */
    void forEachMutable(const std::function<void(UserID, SceneUser&)>& fn);

private:
    SceneUserManager() = default;

    std::unordered_map<UserID, std::shared_ptr<SceneUser>> m_users; /**< 在线用户表 */
};
