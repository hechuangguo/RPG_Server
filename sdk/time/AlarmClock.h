/**
 * @file    AlarmClock.h
 * @brief  闹钟 / 定时触发器 —— 基于墙钟时间与 TimerMgr 单线程调度
 *
 * 与 TimerMgr 的关系：
 * - TimerMgr：相对间隔（「3 秒后」「每 60 秒」），使用 steady_clock
 * - AlarmClock：绝对墙钟时刻（「每天 08:00」），使用 TimeUtil
 *
 * 推荐主循环：
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
#include "../timer/TimerMgr.h"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

using AlarmID = uint64_t;
constexpr AlarmID INVALID_ALARM_ID = 0;
using AlarmCallback = std::function<void()>;

enum class AlarmRepeat : uint8_t
{
    ONCE   = 0,
    DAILY  = 1,
    WEEKLY = 2,
};

class AlarmClock
{
public:
    static AlarmClock& Instance()
    {
        static AlarmClock s;
        return s;
    }

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

    AlarmID SetAfterMs(int64_t delayMs, AlarmCallback cb)
    {
        return SetOnce(TimeUtil::UnixMs() + delayMs, std::move(cb));
    }

    AlarmID SetDaily(int hour, int minute, int second, AlarmCallback cb)
    {
        AlarmEntry e;
        e.id      = ++m_nextID;
        e.repeat  = AlarmRepeat::DAILY;
        e.hour    = hour;
        e.minute  = minute;
        e.second  = second;
        e.weekday = -1;
        e.cb      = std::move(cb);
        e.trigger = TimeUtil::NextLocalTime(hour, minute, second, -1);
        m_alarms[e.id] = std::move(e);
        ScheduleTimer(e.id);
        return e.id;
    }

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

    void Cancel(AlarmID id)
    {
        auto it = m_alarms.find(id);
        if (it == m_alarms.end()) return;
        if (it->second.timerID != INVALID_TIMER_ID)
            TimerMgr::Instance().Cancel(it->second.timerID);
        m_alarms.erase(it);
    }

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
    struct AlarmEntry
    {
        AlarmID       id = INVALID_ALARM_ID;
        AlarmRepeat   repeat = AlarmRepeat::ONCE;
        int64_t       trigger = 0;
        int           hour = 0;
        int           minute = 0;
        int           second = 0;
        int           weekday = -1;
        AlarmCallback cb;
        TimerID       timerID = INVALID_TIMER_ID;
        bool          pendingReschedule = false;
    };

    AlarmClock() : m_nextID(0) {}

    void ScheduleTimer(AlarmID alarmID)
    {
        auto it = m_alarms.find(alarmID);
        if (it == m_alarms.end()) return;

        AlarmEntry& e = it->second;
        if (e.timerID != INVALID_TIMER_ID)
            TimerMgr::Instance().Cancel(e.timerID);

        int64_t now = TimeUtil::UnixMs();
        int64_t delay = e.trigger - now;
        if (delay < 0) delay = 0;

        e.timerID = TimerMgr::Instance().Register(
            static_cast<uint64_t>(delay), 0,
            [this, alarmID]() { OnAlarmFire(alarmID); });
    }

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

    AlarmID m_nextID;
    std::unordered_map<AlarmID, AlarmEntry> m_alarms;
};
