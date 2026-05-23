#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <chrono>

// ============================================================
//  定时器管理（单线程，基于时间戳，在主循环中驱动）
// ============================================================

using TimerID = uint64_t;
using TimerCallback = std::function<void()>;
constexpr TimerID INVALID_TIMER_ID = 0;

class TimerMgr
{
public:
    static TimerMgr& Instance()
    {
        static TimerMgr s;
        return s;
    }

    // 注册定时器
    // interval_ms  : 首次触发延迟（毫秒）
    // repeat_ms    : 重复间隔（0=不重复）
    TimerID Register(uint64_t interval_ms, uint64_t repeat_ms, TimerCallback cb)
    {
        TimerID id = ++m_nextID;
        TimerEntry e;
        e.id         = id;
        e.nextTick   = NowMs() + interval_ms;
        e.repeatMs   = repeat_ms;
        e.cb         = std::move(cb);
        m_timers.emplace(e.nextTick, e);
        return id;
    }

    // 取消定时器
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

    // 主循环中调用
    void Update()
    {
        uint64_t now = NowMs();
        while (!m_timers.empty())
        {
            auto it = m_timers.begin();
            if (it->first > now) break;
            TimerEntry e = it->second;
            m_timers.erase(it);
            e.cb();
            if (e.repeatMs > 0)
            {
                e.nextTick = now + e.repeatMs;
                m_timers.emplace(e.nextTick, e);
            }
        }
    }

    static uint64_t NowMs()
    {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<milliseconds>(
                steady_clock::now().time_since_epoch()).count());
    }

private:
    struct TimerEntry
    {
        TimerID       id;
        uint64_t      nextTick;
        uint64_t      repeatMs;
        TimerCallback cb;
    };

    TimerMgr() : m_nextID(0) {}
    TimerID                        m_nextID;
    std::multimap<uint64_t, TimerEntry> m_timers;
};
