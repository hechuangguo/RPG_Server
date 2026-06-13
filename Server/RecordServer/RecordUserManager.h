/**
 * @file    RecordUserManager.h
 * @brief  RecordServer 用户缓存管理器
 */

#pragma once
#include "RecordUser.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>

/**
 * @brief RecordServer 用户数据缓存（userId → RecordUser）
 */
class RecordUserManager
{
public:
    /** @brief 缓存中是否已有该 userId */
    bool contains(UserID userId) const;

    /** @brief 按 userId 查找 RecordUser */
    std::shared_ptr<RecordUser> findUser(UserID userId) const;

    /** @brief 写入或覆盖缓存条目 */
    bool addUser(UserID userId, std::shared_ptr<RecordUser> user);

    /** @brief 从缓存移除用户 */
    bool removeUser(UserID userId);

    /** @brief 缓存用户数量 */
    size_t getUserCount() const;

    /** @brief 遍历并提供可写 RecordUser 引用 */
    void forEach(const std::function<void(UserID, RecordUser&)>& fn);

private:
    std::unordered_map<UserID, std::shared_ptr<RecordUser>> m_users; /**< Record 用户缓存表 */
};
