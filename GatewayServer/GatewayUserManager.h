/**
 * @file    GatewayUserManager.h
 * @brief  GatewayServer 客户端会话管理器（connId 索引）
 */

#pragma once
#include "GatewayUser.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * @brief Gateway 客户端会话表（connId → GatewayUser）
 */
class GatewayUserManager
{
public:
    /** @brief 按连接 ID 查找会话（不存在返回 nullptr） */
    std::shared_ptr<GatewayUser> findUser(ConnID connId) const;

    /** @brief 为新连接创建并注册会话 */
    std::shared_ptr<GatewayUser> addUser(ConnID connId);

    /** @brief 获取会话引用（不存在时抛 std::out_of_range） */
    GatewayUser& getUser(ConnID connId);

    /** @brief 移除会话，返回是否曾存在 */
    bool removeUser(ConnID connId);

    /** @brief 当前在线会话数 */
    size_t getUserCount() const;

    /** @brief 收集心跳超时的 connId 列表 */
    std::vector<ConnID> collectExpiredConnIds(uint64_t nowMs, uint64_t timeoutMs) const;

    /** @brief 遍历全部会话并提供可写引用 */
    void forEach(const std::function<void(ConnID, GatewayUser&)>& fn);

private:
    std::unordered_map<ConnID, std::shared_ptr<GatewayUser>> m_users; /**< connId -> 会话对象 */
};
