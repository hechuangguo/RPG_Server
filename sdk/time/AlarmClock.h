/**
 * @file    AlarmClock.h
 * @brief  闹钟 / 定时触发器 —— 基于墙钟时间与 TimerMgr 单线程调度
 *
 * 与 TimerMgr 的关系：
 * - TimerMgr：相对间隔（「3 秒后」「每 60 秒」），使用 steady_clock
 * - AlarmClock：绝对墙钟时刻（「每天 08:00」），使用 TimeUtil
 *
 * 实现原理：
 * - 每个闹钟注册为一条 AlarmEntry，内部通过 TimerMgr::Register() 设置一个
 *   到达目标时刻的倒计时定时器。
 * - 定时器触发时调用 OnAlarmFire()：执行用户回调，并根据重复模式（ONCE/DAILY/WEEKLY）
 *   决定是否重新调度。重复闹钟不直接在回调中重调度（避免栈溢出），而是设置
 *   pendingReschedule 标志，由 Update() 在下一帧统一处理。
 *
 * 精度特性：
 * - 闹钟精度 = TimerMgr::Update() 的调用间隔 + 系统调度延迟。
 * - 本质上是「相对定时器 → 绝对时刻」的桥接层，不独立维护优先队列。
 *
 * 性能特性：
 * - 使用 unordered_map 存储 AlarmEntry，增删查找均 O(1)。
 * - Cancel() 会同步取消底层 TimerMgr 定时器，避免悬空回调。
 * - 整体无堆分配热点（回调通过 std::function 内部 small buffer optimization）。
 *
 * 线程安全：
 * - **非线程安全**。所有方法必须在同一线程（主循环）中调用。
 * - 推荐主循环：
 * @code
 *   while (true) {
 *       server.Poll();
 *       TimerMgr::Instance().Update();
 *       AlarmClock::Instance().Update();
 *   }
 * @endcode
 */

#pragma once
#include "TimeUtil.h"
#include "../util/Singleton.h"
#include "../timer/TimerMgr.h"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

/** @brief 闹钟 ID 类型，0 表示无效 */
using AlarmID = uint64_t;
/** @brief 无效闹钟 ID 常量 */
constexpr AlarmID INVALID_ALARM_ID = 0;
/** @brief 闹钟触发时的回调签名（无参数、无返回值） */
using AlarmCallback = std::function<void()>;

/**
 * @brief 闹钟重复模式
 *
 * 控制闹钟触发后的行为：一次性、每日重复、每周重复。
 */
enum class AlarmRepeat : uint8_t
{
    ONCE   = 0,  /**< 仅触发一次，触发后自动移除 */
    DAILY  = 1,  /**< 每天在指定时刻重复触发 */
    WEEKLY = 2,  /**< 每周在指定星期和时刻重复触发 */
};

/**
 * @brief 全局闹钟管理器（单例）
 *
 * 提供基于墙钟时间的定时触发能力，支持一次性、每日、每周三种模式。
 * 内部依赖 TimerMgr 进行底层定时驱动，通过 TimeUtil 计算下一个目标时刻。
 *
 * 使用示例：
 * @code
 *   // 5 秒后触发一次
 *   AlarmClock::Instance().SetAfterMs(5000, []{ std::cout << "timeout!\n"; });
 *
 *   // 每天 08:00 执行
 *   AlarmClock::Instance().SetDaily(8, 0, 0, []{ ReloadConfig(); });
 *
 *   // 每周一 09:30 执行
 *   AlarmClock::Instance().SetWeekly(1, 9, 30, 0, []{ WeeklyReport(); });
 * @endcode
 */
class AlarmClock : public LazySingleton<AlarmClock>
{
public:
    friend class LazySingleton<AlarmClock>;
    /** @brief 获取全局唯一实例（与既有调用方式兼容） */
    static AlarmClock& Instance() { return LazySingleton<AlarmClock>::Instance(); }

    /**
     * @brief 设置一次性闹钟（指定绝对 Unix 毫秒时间戳）
     * @param triggerUnixMs 触发时刻的 Unix 毫秒时间戳
     * @param cb            触发时执行的回调
     * @return 新创建的闹钟 ID；可用于 Cancel() 取消
     * @note  若 triggerUnixMs 已过去（delay < 0），将在下一帧 Update() 中立即触发
     */
    AlarmID SetOnce(int64_t triggerUnixMs, AlarmCallback cb)
    {
        AlarmEntry e;
        e.id      = ++m_nextID;
        e.repeat  = AlarmRepeat::ONCE;
        e.trigger = triggerUnixMs;
        e.cb      = std::move(cb);
        m_alarms[e.id] = std::move(e);
        ScheduleTimer(e.id);
        return e.id;
    }

    /**
     * @brief 设置一次性闹钟（相对延迟，基于当前时间）
     * @param delayMs 延迟毫秒数
     * @param cb      触发时执行的回调
     * @return 新创建的闹钟 ID
     * @note  等价于 SetOnce(TimeUtil::UnixMs() + delayMs, cb)
     */
    AlarmID SetAfterMs(int64_t delayMs, AlarmCallback cb)
    {
        return SetOnce(TimeUtil::UnixMs() + delayMs, std::move(cb));
    }

    /**
     * @brief 设置每日闹钟
     * @param hour   小时（0-23）
     * @param minute 分钟（0-59）
     * @param second 秒（0-59）
     * @param cb     触发时执行的回调
     * @return 新创建的闹钟 ID
     * @note  首次触发时刻为当天的指定时间（已过则为明天），之后每天重复
     */
    AlarmID SetDaily(int hour, int minute, int second, AlarmCallback cb)
    {
        AlarmEntry e;
        e.id      = ++m_nextID;
        e.repeat  = AlarmRepeat::DAILY;
        e.hour    = hour;
        e.minute  = minute;
        e.second  = second;
        e.weekday = -1;  /**< 每日模式不限制星期 */
        e.cb      = std::move(cb);
        e.trigger = TimeUtil::NextLocalTime(hour, minute, second, -1);
        m_alarms[e.id] = std::move(e);
        ScheduleTimer(e.id);
        return e.id;
    }

    /**
     * @brief 设置每周闹钟
     * @param weekday 星期几（0=周日 .. 6=周六，同 tm_wday 约定）
     * @param hour    小时（0-23）
     * @param minute  分钟（0-59）
     * @param second  秒（0-59）
     * @param cb      触发时执行的回调
     * @return 新创建的闹钟 ID
     * @note  首次触发时刻为最近的指定星期+时刻，之后每周重复
     */
    AlarmID SetWeekly(int weekday, int hour, int minute, int second,
                      AlarmCallback cb)
    {
        AlarmEntry e;
        e.id      = ++m_nextID;
        e.repeat  = AlarmRepeat::WEEKLY;
        e.hour    = hour;
        e.minute  = minute;
        e.second  = second;
        e.weekday = weekday;
        e.cb      = std::move(cb);
        e.trigger = TimeUtil::NextLocalTime(hour, minute, second, weekday);
        m_alarms[e.id] = std::move(e);
        ScheduleTimer(e.id);
        return e.id;
    }

    /**
     * @brief 取消指定闹钟
     * @param id 闹钟 ID（由 SetOnce/SetAfterMs/SetDaily/SetWeekly 返回）
     * @note  同时取消底层 TimerMgr 定时器，避免后续误触发；幂等操作
     */
    void Cancel(AlarmID id)
    {
        auto it = m_alarms.find(id);
        if (it == m_alarms.end()) return;
        if (it->second.timerID != INVALID_TIMER_ID)
            TimerMgr::Instance().Cancel(it->second.timerID);
        m_alarms.erase(it);
    }

    /**
     * @brief 取消所有闹钟
     * @note  遍历所有闹钟并逐一取消底层定时器，最后清空容器
     */
    void CancelAll()
    {
        for (auto& [id, e] : m_alarms)
        {
            (void)id;
            if (e.timerID != INVALID_TIMER_ID)
                TimerMgr::Instance().Cancel(e.timerID);
        }
        m_alarms.clear();
    }

    /**
     * @brief 单帧驱动 —— 必须在主循环中每帧调用
     *
     * 处理重复闹钟的重调度：
     * - DAILY / WEEKLY 闹钟触发后，OnAlarmFire() 仅设置 pendingReschedule 标志，
     *   而不直接重调 ScheduleTimer()（避免在定时器回调栈中嵌套注册新定时器）。
     * - 本方法收集所有 pendingReschedule 的闹钟，统一调用 ScheduleTimer() 重新注册。
     *
     * @note  需与 TimerMgr::Instance().Update() 配合使用，调用顺序无严格要求
     */
    void Update()
    {
        std::vector<AlarmID> pending;
        for (auto& [id, e] : m_alarms)
        {
            if (e.repeat != AlarmRepeat::ONCE && e.pendingReschedule)
            {
                e.pendingReschedule = false;
                pending.push_back(id);
            }
        }
        for (AlarmID id : pending)
            ScheduleTimer(id);
    }

private:
    /**
     * @brief 闹钟条目内部结构
     *
     * 记录单个闹钟的全部信息：ID、重复模式、触发时刻、回调、
     * 以及关联的 TimerMgr 定时器 ID。
     */
    struct AlarmEntry
    {
        AlarmID       id = INVALID_ALARM_ID;   /**< 闹钟唯一标识 */
        AlarmRepeat   repeat = AlarmRepeat::ONCE; /**< 重复模式 */
        int64_t       trigger = 0;              /**< 下一次触发的 Unix 毫秒时间戳 */
        int           hour = 0;                 /**< 目标小时（DAILY/WEEKLY 使用） */
        int           minute = 0;               /**< 目标分钟（DAILY/WEEKLY 使用） */
        int           second = 0;               /**< 目标秒（DAILY/WEEKLY 使用） */
        int           weekday = -1;             /**< 目标星期（WEEKLY 使用，-1 表示不限制） */
        AlarmCallback cb;                       /**< 用户回调 */
        TimerID       timerID = INVALID_TIMER_ID; /**< 关联的 TimerMgr 定时器 ID */
        bool          pendingReschedule = false; /**< 是否等待 Update() 重调度 */
    };
    /** @brief 私有构造（单例模式） */
    AlarmClock() : m_nextID(0) {}

    /**
     * @brief 为指定闹钟注册（或重新注册）TimerMgr 定时器
     *
     * 计算当前时间到目标时刻的延迟，通过 TimerMgr::Register() 注册一次性定时器。
     * 如果已有旧定时器，先取消再注册。
     *
     * @param alarmID 闹钟 ID
     */
    void ScheduleTimer(AlarmID alarmID)
    {
        auto it = m_alarms.find(alarmID);
        if (it == m_alarms.end()) return;
        AlarmEntry& e = it->second;
        if (e.timerID != INVALID_TIMER_ID)
            TimerMgr::Instance().Cancel(e.timerID);
        int64_t now = TimeUtil::UnixMs();
        int64_t delay = e.trigger - now;
        if (delay < 0) delay = 0;  /**< 已过时刻，立即触发 */
        e.timerID = TimerMgr::Instance().Register(
            static_cast<uint64_t>(delay), 0,
            [this, alarmID]() { OnAlarmFire(alarmID); });
    }

    /**
     * @brief 定时器触发时的回调（由 TimerMgr 调用）
     *
     * 执行流程：
     * 1. 清除 timerID（标记底层定时器已消耗）
     * 2. 执行用户回调 cb()
     * 3. 根据重复模式决定后续行为：
     *    - ONCE：从 m_alarms 中移除此条目
     *    - DAILY：计算明天的同一时刻，设置 pendingReschedule 等待 Update() 重调度
     *    - WEEKLY：计算下周同一星期+时刻，设置 pendingReschedule 等待 Update() 重调度
     *
     * @param alarmID 触发的闹钟 ID
     */
    void OnAlarmFire(AlarmID alarmID)
    {
        auto it = m_alarms.find(alarmID);
        if (it == m_alarms.end()) return;
        AlarmEntry& e = it->second;
        e.timerID = INVALID_TIMER_ID;
        if (e.cb) e.cb();
        switch (e.repeat)
        {
        case AlarmRepeat::ONCE:
            m_alarms.erase(it);
            break;
        case AlarmRepeat::DAILY:
            e.trigger = TimeUtil::NextLocalTime(e.hour, e.minute, e.second, -1);
            e.pendingReschedule = true;
            break;
        case AlarmRepeat::WEEKLY:
            e.trigger = TimeUtil::NextLocalTime(
                e.hour, e.minute, e.second, e.weekday);
            e.pendingReschedule = true;
            break;
        }
    }
    AlarmID m_nextID;  /**< 闹钟 ID 自增计数器 */
    std::unordered_map<AlarmID, AlarmEntry> m_alarms;  /**< 所有活跃闹钟的存储 */
};
