/**
 * @file    TimerMgr.h
 * @brief  单线程定时器管理器
 *
 * 基于 std::multimap<uint64_t, TimerEntry> 实现的最小堆风格定时器队列。
 * 所有操作在主循环线程内完成，无需加锁。
 *
 * 使用方式：
 * @code
 *   // 一次性定时器（3 秒后触发）
 *   TimerMgr::Instance().Register(3000, 0, []{ LOG_INFO("3 seconds passed"); });
 *
 *   // 重复定时器（每 60 秒触发）
 *   TimerMgr::Instance().Register(60000, 60000, []{ AutoSaveAll(); });
 *
 *   // 主循环
 *   while (true) { TimerMgr::Instance().Update(); ... }
 * @endcode
 */

#pragma once
#include "../util/Singleton.h"
#include <cstdint>
#include <functional>
#include <map>
#include <chrono>

/** @brief 定时器唯一标识 */
using TimerID = uint64_t;

/** @brief 定时器回调函数类型 */
using TimerCallback = std::function<void()>;

/** @brief 无效定时器 ID */
constexpr TimerID INVALID_TIMER_ID = 0;

class TimerMgr : public LazySingleton<TimerMgr>
{
public:
    friend class LazySingleton<TimerMgr>;
    /** @brief 获取全局唯一实例（与既有调用方式兼容） */
    static TimerMgr& Instance() { return LazySingleton<TimerMgr>::Instance(); }

    /**
     * @brief 注册定时器
     * @param interval_ms 首次触发延迟（毫秒）
     * @param repeat_ms   重复间隔（毫秒）；0 表示仅触发一次
     * @param cb          回调函数（std::function，支持 lambda）
     * @return 定时器 ID，可用于 Cancel()
     */
    TimerID Register(uint64_t interval_ms, uint64_t repeat_ms, TimerCallback cb)
    {
        TimerID id = ++m_nextID;
        TimerEntry e;
        e.id         = id;
        e.nextTick   = NowMs() + interval_ms;
        e.repeatMs   = repeat_ms;
        e.cb         = std::move(cb);
        m_timers.emplace(e.nextTick, e);  /**< 按触发时间排序插入 */
        return id;
    }

    /**
     * @brief 取消定时器
     * @param id Register() 返回的定时器 ID
     * @note  遍历删除，O(n)，适合少量定时器场景
     */
    void Cancel(TimerID id)
    {
        for (auto it = m_timers.begin(); it != m_timers.end(); )
        {
            if (it->second.id == id)
                it = m_timers.erase(it);
            else
                ++it;
        }
    }

    /**
     * @brief 驱动定时器（必须在主循环中每帧调用）
     *
     * 取出所有到期（nextTick <= now）的定时器，执行回调。
     * 重复定时器会自动重新排入队列。
     */
    void Update()
    {
        uint64_t now = NowMs();
        while (!m_timers.empty())
        {
            auto it = m_timers.begin();
            if (it->first > now) break;  /**< 未到期，提前退出 */
            TimerEntry e = it->second;
            m_timers.erase(it);
            e.cb();  /**< 执行回调（可能注册/取消定时器，不能持有迭代器） */
            if (e.repeatMs > 0)
            {
                e.nextTick = now + e.repeatMs;
                m_timers.emplace(e.nextTick, e);
            }
        }
    }

    /**
     * @brief 获取当前毫秒时间戳（steady_clock，不受系统时间调整影响）
     * @return 从 steady_clock epoch 以来的毫秒数
     * @see TimeUtil::UnixMs() 墙钟 Unix 毫秒，用于日志与闹钟
     */
    static uint64_t NowMs()
    {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<milliseconds>(
                steady_clock::now().time_since_epoch()).count());
    }

private:
    /** @brief 定时器条目 */
    struct TimerEntry
    {
        TimerID       id;        /**< 唯一标识 */
        uint64_t      nextTick;  /**< 下次触发时间（ms） */
        uint64_t      repeatMs;  /**< 重复间隔（0=一次性） */
        TimerCallback cb;        /**< 回调 */
    };

    TimerMgr() : m_nextID(0) {}

    TimerID                        m_nextID;  /**< 自增 ID 分配器 */
    std::multimap<uint64_t, TimerEntry> m_timers; /**< 按 nextTick 排序的定时器队列 */
};
