/**
 * @file    Logger.h
 * @brief  全局日志系统（线程安全单例）
 *
 * 特性：
 * - 五级日志：DEBUG / INFO / WARN / ERROR / FATAL
 * - 同时输出到 stdout 和文件
 * - 线程安全（std::mutex）
 * - 每行自动附加时间戳和服务器名称前缀
 * - 便捷宏：LOG_DEBUG / LOG_INFO / LOG_WARN / LOG_ERR / LOG_FATAL
 *
 * 输出格式：
 * @code
 *   [2026-05-23 20:30:00][SuperServer][INFO] Server started on 0.0.0.0:9000
 * @endcode
 */

#pragma once
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstdarg>
#include <string>
#include <mutex>

/**
 * @brief 日志级别枚举（数值越大越严重）
 */
enum class LogLevel : int
{
    DEBUG  = 0,  /**< 调试信息（仅开发环境） */
    INFO   = 1,  /**< 常规运行信息 */
    WARN   = 2,  /**< 警告（不影响运行但需关注） */
    ERR    = 3,  /**< 错误（功能可能受损） */
    FATAL  = 4,  /**< 致命错误（将触发 fflush，建议随后 exit） */
};

/**
 * @brief 日志管理器（单例）
 *
 * 使用方式：
 * @code
 *   Logger::Instance().SetServerName("SuperServer");
 *   Logger::Instance().SetPath("logs/super.log");
 *   LOG_INFO("Server started on port %d", 9000);
 * @endcode
 */
class Logger
{
public:
    /** @brief 获取全局唯一实例（线程安全的静态局部变量） */
    static Logger& Instance()
    {
        static Logger s;
        return s;
    }

    /** @brief 设置最低输出级别（低于此级别的日志将被忽略） */
    void SetLevel(LogLevel lv)  { m_level = lv;  }

    /**
     * @brief 设置日志文件路径
     * @param path 相对或绝对路径；空字符串则仅输出到 stdout
     * @note  会关闭之前打开的文件句柄
     */
    void SetPath(const std::string& path)
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_fp) { fclose(m_fp); m_fp = nullptr; }
        if (!path.empty())
            m_fp = fopen(path.c_str(), "a");
        m_path = path;
    }

    /**
     * @brief 设置日志中的服务器标识名
     * @param name 如 "SuperServer" / "SceneServer"
     */
    void SetServerName(const std::string& name) { m_name = name; }

    /**
     * @brief 记录一条日志
     * @param lv  日志级别
     * @param fmt printf 格式字符串
     * @param ... 可变参数
     */
    void Log(LogLevel lv, const char* fmt, ...)
    {
        if (lv < m_level) return;
        char buf[2048];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);

        // 格式化时间戳
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
        fwrite(line, 1, n, stdout);             /**< 总是输出到 stdout */
        if (m_fp) fwrite(line, 1, n, m_fp);     /**< 同时写文件 */
        if (lv == LogLevel::FATAL) fflush(m_fp ? m_fp : stdout); /**< FATAL 立即刷新 */
    }

private:
    Logger() : m_fp(nullptr), m_level(LogLevel::DEBUG), m_name("Server") {}
    ~Logger() { if (m_fp) fclose(m_fp); }

    FILE*       m_fp;     /**< 日志文件句柄（nullptr = 仅 stdout） */
    LogLevel    m_level;  /**< 当前最低输出级别 */
    std::string m_name;   /**< 服务器名称前缀 */
    std::string m_path;   /**< 日志文件路径 */
    std::mutex  m_mu;     /**< 写文件互斥锁 */
};

// ============================================================
//  便捷宏 —— 简化调用
// ============================================================
#define LOG_DEBUG(fmt, ...) Logger::Instance().Log(LogLevel::DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  Logger::Instance().Log(LogLevel::INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Logger::Instance().Log(LogLevel::WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   Logger::Instance().Log(LogLevel::ERR,   fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) Logger::Instance().Log(LogLevel::FATAL, fmt, ##__VA_ARGS__)
