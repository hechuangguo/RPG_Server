/**
 * @file    BuffManager.h
 * @brief  状态/Buff 管理器（运行时，默认不持久化）
 *
 * loop 中剔除过期 Buff；expireAtMs 由 add 时写入（当前为相对毫秒占位，可改为 now+duration）。
 */

#pragma once
#include <cstdint>
#include <unordered_map>

/** @brief 单个 Buff 实例 */
struct BuffState
{
    uint64_t expireAtMs = 0; /**< 到期时间戳（毫秒），0 表示永久直到 remove */
};

/**
 * @brief Buff 列表管理器
 */
class BuffManager
{
public:
    /** @brief 初始化 Buff 容器 */
    bool init();
    /** @brief 每帧剔除到期 Buff */
    void loop(uint64_t nowMs);
    /** @brief 是否有待保存 Buff 变更 */
    bool needSave() const;
    /** @brief 保存 Buff 状态并清脏 */
    bool save();
    /** @brief 加载 Buff 状态 */
    bool load();

    /**
     * @brief 添加 Buff
     * @param buffId      Buff 模板 ID
     * @param durationMs  占位：写入 expireAtMs（后续可改为绝对时间）
     */
    bool add(uint32_t buffId, uint32_t durationMs = 0);

    /** @brief 移除指定 Buff（unused 预留协议兼容） */
    bool remove(uint32_t buffId, uint32_t unused = 0);

private:
    std::unordered_map<uint32_t, BuffState> buffMap; /**< buffId -> 状态 */
    bool dirty = false;                               /**< 脏标记 */
};
