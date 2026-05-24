/**
 * @file    TimeUtil.h
 * @brief  时间工具库 —— 时间戳转换、分量提取、间隔计算、格式化输出
 *
 * 约定：
 * - 时间戳基准为 **Unix 纪元**（1970-01-01 00:00:00 UTC），内部统一使用 **毫秒**（int64_t）。
 * - 分量提取（年/月/日/时/分/秒/星期）默认按 **本地时区**（localtime_r）。
 * - 与 TimerMgr::NowMs() 不同：TimerMgr 使用 steady_clock（单调时钟，适合间隔计时）；
 *   TimeUtil 使用 system_clock（墙钟，适合日志、活动、闹钟）。
 *
 * 使用示例：
 * @code
 *   int64_t now = TimeUtil::UnixMs();
 *   std::string s = TimeUtil::ToString(now);
 *   std::string f = TimeUtil::Format(now, "%Y/%m/%d %H:%M:%S");
 *
 *   int64_t ts = 0;
 *   TimeUtil::Parse("2025-05-24 12:30:00", ts);
 *
 *   int days   = TimeUtil::DaysBetween(ts1, ts2);
 *   int months = TimeUtil::MonthsBetween(ts1, ts2);
 * @endcode
 */

#pragma once

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

/** @brief 默认日期时间字符串格式 */
constexpr const char* TIME_DEFAULT_FMT = "%Y-%m-%d %H:%M:%S";

/**
 * @brief 日历时间结构（本地或 UTC 解析结果）
 */
struct DateTimeParts
{
    int year    = 1970;  /**< 年 */
    int month   = 1;     /**< 月，1-12 */
    int day     = 1;     /**< 日，1-31 */
    int hour    = 0;     /**< 时，0-23 */
    int minute  = 0;     /**< 分，0-59 */
    int second  = 0;     /**< 秒，0-59 */
    int weekday = 0;     /**< 星期，0=周日 .. 6=周六（同 tm_wday） */
};

/**
 * @brief 时间工具类（静态方法，无状态）
 */
class TimeUtil
{
public:
    // ============================================================
    //  当前墙钟时间戳
    // ============================================================

    static int64_t UnixUs()
    {
        using namespace std::chrono;
        return duration_cast<microseconds>(
            system_clock::now().time_since_epoch()).count();
    }

    static int64_t UnixMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
    }

    static int64_t UnixSec()
    {
        using namespace std::chrono;
        return duration_cast<seconds>(
            system_clock::now().time_since_epoch()).count();
    }

    static int NowSecond()  { return Second(UnixMs()); }
    static int NowMinute()  { return Minute(UnixMs()); }
    static int NowHour()    { return Hour(UnixMs()); }
    static int NowWeekday() { return Weekday(UnixMs()); }
    static int NowDay()     { return Day(UnixMs()); }
    static int NowMonth()   { return Month(UnixMs()); }
    static int NowYear()    { return Year(UnixMs()); }

    // ============================================================
    //  时间戳 → 字符串 / 格式化
    // ============================================================

    /** @brief 默认格式 "YYYY-MM-DD HH:MM:SS"（本地时区） */
    static std::string ToString(int64_t unixMs, bool useUtc = false)
    {
        return Format(unixMs, TIME_DEFAULT_FMT, useUtc);
    }

    /**
     * @brief 按 strftime 格式输出；扩展占位符 %ms（毫秒）、%us（微秒）
     */
    static std::string Format(int64_t unixMs, const char* fmt,
                              bool useUtc = false)
    {
        if (!fmt) return {};

        time_t sec = static_cast<time_t>(unixMs / 1000);
        int msPart = static_cast<int>(unixMs % 1000);
        if (msPart < 0) msPart += 1000;

        struct tm tmBuf{};
        FillTm(sec, useUtc, tmBuf);

        std::string pattern(fmt);
        ReplaceToken(pattern, "%ms", MsToken(msPart));
        ReplaceToken(pattern, "%us", UsToken(msPart));

        char buf[256] = {};
        strftime(buf, sizeof(buf), pattern.c_str(), &tmBuf);
        return buf;
    }

    // ============================================================
    //  字符串 → 时间戳
    // ============================================================

    static bool Parse(const std::string& str, int64_t& outUnixMs,
                      bool asUtc = false)
    {
        return Parse(str, TIME_DEFAULT_FMT, outUnixMs, asUtc);
    }

    static bool Parse(const std::string& str, const char* fmt,
                      int64_t& outUnixMs, bool asUtc = false)
    {
        if (!fmt || str.empty()) return false;

        std::tm tm{};
        tm.tm_isdst = -1;
        std::istringstream iss(str);
        iss >> std::get_time(&tm, fmt);
        if (iss.fail()) return false;

        time_t sec = asUtc ? TimeGm(&tm) : std::mktime(&tm);
        if (sec == static_cast<time_t>(-1)) return false;

        outUnixMs = static_cast<int64_t>(sec) * 1000;
        return true;
    }

    /** @brief 解析 "YYYY-MM-DD HH:MM:SS.mmm" 形式 */
    static bool ParseWithMs(const std::string& str, const char* fmt,
                            int64_t& outUnixMs, bool asUtc = false)
    {
        std::string mainPart = str;
        int ms = 0;
        size_t dot = str.rfind('.');
        if (dot != std::string::npos && dot + 1 < str.size())
        {
            mainPart = str.substr(0, dot);
            std::string frac = str.substr(dot + 1);
            if (frac.size() > 3) frac = frac.substr(0, 3);
            while (frac.size() < 3) frac += '0';
            ms = atoi(frac.c_str());
        }
        if (!Parse(mainPart, fmt, outUnixMs, asUtc)) return false;
        outUnixMs += ms;
        return true;
    }

    // ============================================================
    //  时间戳 → 日历分量
    // ============================================================

    static DateTimeParts ToLocalParts(int64_t unixMs)
    {
        return ToParts(unixMs, false);
    }

    static DateTimeParts ToUtcParts(int64_t unixMs)
    {
        return ToParts(unixMs, true);
    }

    static int Year(int64_t unixMs, bool useUtc = false)
    { return ToParts(unixMs, useUtc).year; }

    static int Month(int64_t unixMs, bool useUtc = false)
    { return ToParts(unixMs, useUtc).month; }

    static int Day(int64_t unixMs, bool useUtc = false)
    { return ToParts(unixMs, useUtc).day; }

    static int Hour(int64_t unixMs, bool useUtc = false)
    { return ToParts(unixMs, useUtc).hour; }

    static int Minute(int64_t unixMs, bool useUtc = false)
    { return ToParts(unixMs, useUtc).minute; }

    static int Second(int64_t unixMs, bool useUtc = false)
    { return ToParts(unixMs, useUtc).second; }

    /** @brief 0=周日 .. 6=周六 */
    static int Weekday(int64_t unixMs, bool useUtc = false)
    { return ToParts(unixMs, useUtc).weekday; }

    /** @brief ISO：1=周一 .. 7=周日 */
    static int IsoWeekday(int64_t unixMs, bool useUtc = false)
    {
        int w = Weekday(unixMs, useUtc);
        return w == 0 ? 7 : w;
    }

    // ============================================================
    //  边界时间戳（该时刻起始的 Unix 毫秒）
    // ============================================================

    static int64_t StartOfMinute(int64_t unixMs, bool useUtc = false)
    {
        auto p = ToParts(unixMs, useUtc);
        return FromParts(p.year, p.month, p.day, p.hour, p.minute, 0, useUtc);
    }

    static int64_t StartOfHour(int64_t unixMs, bool useUtc = false)
    {
        auto p = ToParts(unixMs, useUtc);
        return FromParts(p.year, p.month, p.day, p.hour, 0, 0, useUtc);
    }

    static int64_t StartOfDay(int64_t unixMs, bool useUtc = false)
    {
        auto p = ToParts(unixMs, useUtc);
        return FromParts(p.year, p.month, p.day, 0, 0, 0, useUtc);
    }

    static int64_t StartOfMonth(int64_t unixMs, bool useUtc = false)
    {
        auto p = ToParts(unixMs, useUtc);
        return FromParts(p.year, p.month, 1, 0, 0, 0, useUtc);
    }

    static int64_t StartOfYear(int64_t unixMs, bool useUtc = false)
    {
        auto p = ToParts(unixMs, useUtc);
        return FromParts(p.year, 1, 1, 0, 0, 0, useUtc);
    }

    // ============================================================
    //  两个时间戳之间的间隔
    // ============================================================

    /** @brief 日历天数差（按日边界，可正可负） */
    static int DaysBetween(int64_t unixMs1, int64_t unixMs2, bool useUtc = false)
    {
        int64_t d1 = StartOfDay(unixMs1, useUtc);
        int64_t d2 = StartOfDay(unixMs2, useUtc);
        return static_cast<int>((d2 - d1) / 86400000LL);
    }

    /** @brief 完整日历月数差（可正可负） */
    static int MonthsBetween(int64_t unixMs1, int64_t unixMs2, bool useUtc = false)
    {
        if (unixMs1 > unixMs2) return -MonthsBetween(unixMs2, unixMs1, useUtc);

        auto a = ToParts(unixMs1, useUtc);
        auto b = ToParts(unixMs2, useUtc);
        int months = (b.year - a.year) * 12 + (b.month - a.month);
        if (b.day < a.day) --months;
        return months;
    }

    static int64_t SecondsBetween(int64_t unixMs1, int64_t unixMs2)
    {
        return (unixMs2 - unixMs1) / 1000;
    }

    static int64_t FromParts(int year, int month, int day,
                             int hour, int minute, int second,
                             bool asUtc = false)
    {
        struct tm tm{};
        tm.tm_year  = year - 1900;
        tm.tm_mon   = month - 1;
        tm.tm_mday  = day;
        tm.tm_hour  = hour;
        tm.tm_min   = minute;
        tm.tm_sec   = second;
        tm.tm_isdst = -1;

        time_t sec = asUtc ? TimeGm(&tm) : std::mktime(&tm);
        if (sec == static_cast<time_t>(-1)) return 0;
        return static_cast<int64_t>(sec) * 1000;
    }

    /**
     * @brief 计算下一次指定本地时刻的 Unix 毫秒（用于闹钟）
     * @param hour minute second 目标时刻
     * @param weekday 若 0-6 则匹配星期几；传入 -1 表示每天
     */
    static int64_t NextLocalTime(int hour, int minute, int second,
                                 int weekday = -1)
    {
        int64_t now = UnixMs();
        auto p = ToParts(now, false);

        int64_t candidate = FromParts(p.year, p.month, p.day,
                                      hour, minute, second, false);
        if (weekday >= 0)
        {
            while (Weekday(candidate) != weekday || candidate <= now)
                candidate += 86400000LL;
            return candidate;
        }

        if (candidate <= now)
            candidate += 86400000LL;
        return candidate;
    }

private:
    static void FillTm(time_t sec, bool useUtc, struct tm& out)
    {
        if (useUtc)
        {
#if defined(_WIN32)
            gmtime_s(&out, &sec);
#else
            gmtime_r(&sec, &out);
#endif
        }
        else
        {
#if defined(_WIN32)
            localtime_s(&out, &sec);
#else
            localtime_r(&sec, &out);
#endif
        }
    }

    static DateTimeParts ToParts(int64_t unixMs, bool useUtc)
    {
        DateTimeParts p;
        time_t sec = static_cast<time_t>(unixMs / 1000);
        struct tm tmBuf{};
        FillTm(sec, useUtc, tmBuf);

        p.year    = tmBuf.tm_year + 1900;
        p.month   = tmBuf.tm_mon + 1;
        p.day     = tmBuf.tm_mday;
        p.hour    = tmBuf.tm_hour;
        p.minute  = tmBuf.tm_min;
        p.second  = tmBuf.tm_sec;
        p.weekday = tmBuf.tm_wday;
        return p;
    }

    static time_t TimeGm(struct tm* tm)
    {
#if defined(_WIN32)
        return _mkgmtime(tm);
#else
        return timegm(tm);
#endif
    }

    static void ReplaceToken(std::string& s, const char* tok,
                             const std::string& val)
    {
        const size_t len = strlen(tok);
        size_t pos = 0;
        while ((pos = s.find(tok, pos)) != std::string::npos)
        {
            s.replace(pos, len, val);
            pos += val.size();
        }
    }

    static std::string MsToken(int ms)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%03d", ms);
        return buf;
    }

    static std::string UsToken(int ms)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%06d", ms * 1000);
        return buf;
    }
};
