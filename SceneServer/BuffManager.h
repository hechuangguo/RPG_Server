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
    bool init();
    void loop(uint64_t nowMs);
    bool needSave() const;
    bool save();
    bool load();

    /**
     * @brief 添加 Buff
     * @param buffId      Buff 模板 ID
     * @param durationMs  占位：写入 expireAtMs（后续可改为绝对时间）
     */
    bool add(uint32_t buffId, uint32_t durationMs = 0);

    bool remove(uint32_t buffId, uint32_t unused = 0);

private:
    std::unordered_map<uint32_t, BuffState> buffMap;
    bool dirty = false;
};
