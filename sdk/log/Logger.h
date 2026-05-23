#pragma once
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstdarg>
#include <string>
#include <mutex>

// ============================================================
//  日志系统（线程安全，支持输出级别与文件路径）
// ============================================================

enum class LogLevel : int
{
    DEBUG  = 0,
    INFO   = 1,
    WARN   = 2,
    ERR    = 3,
    FATAL  = 4,
};

class Logger
{
public:
    static Logger& Instance()
    {
        static Logger s;
        return s;
    }

    void SetLevel(LogLevel lv)  { m_level = lv;  }
    void SetPath(const std::string& path)
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_fp) { fclose(m_fp); m_fp = nullptr; }
        if (!path.empty())
            m_fp = fopen(path.c_str(), "a");
        m_path = path;
    }
    void SetServerName(const std::string& name) { m_name = name; }

    void Log(LogLevel lv, const char* fmt, ...)
    {
        if (lv < m_level) return;
        char buf[2048];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);

        time_t now = time(nullptr);
        struct tm t{};
        localtime_r(&now, &t);
        char ts[32];
        snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);

        const char* lvStr[] = {"DEBUG","INFO","WARN","ERROR","FATAL"};
        char line[2200];
        int n = snprintf(line, sizeof(line), "[%s][%s][%s] %s\n",
                         ts, m_name.c_str(), lvStr[(int)lv], buf);

        std::lock_guard<std::mutex> lk(m_mu);
        fwrite(line, 1, n, stdout);
        if (m_fp) fwrite(line, 1, n, m_fp);
        if (lv == LogLevel::FATAL) fflush(m_fp ? m_fp : stdout);
    }

private:
    Logger() : m_fp(nullptr), m_level(LogLevel::DEBUG), m_name("Server") {}
    ~Logger() { if (m_fp) fclose(m_fp); }

    FILE*       m_fp;
    LogLevel    m_level;
    std::string m_name;
    std::string m_path;
    std::mutex  m_mu;
};

// 便捷宏
#define LOG_DEBUG(fmt, ...) Logger::Instance().Log(LogLevel::DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  Logger::Instance().Log(LogLevel::INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Logger::Instance().Log(LogLevel::WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   Logger::Instance().Log(LogLevel::ERR,   fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) Logger::Instance().Log(LogLevel::FATAL, fmt, ##__VA_ARGS__)
