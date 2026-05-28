/**
 * @file    Logger.h
 * @brief  全局日志系统（线程安全单例）
 *
 * 特性：
 * - 五级日志：DEBUG / INFO / WARN / ERROR / FATAL
 * - 同时输出到 stdout 和文件
 * - 双文件落盘：logs/aoi.log（实时）+ logs/aoi.log.YYYYMMDD-HH（按小时归档）
 * - 每条日志写入后立即 fflush(stdout) 与刷盘，便于 tail -F 实时查看
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
#include "LogFileWriter.h"
#include "../util/Singleton.h"
#include "../time/TimeUtil.h"
#include <cstdint>
#include <cstdio>
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
    FATAL  = 4,  /**< 致命错误（建议随后 exit） */
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
class Logger : public LazySingleton<Logger>
{
public:
    friend class LazySingleton<Logger>;
    /** @brief 获取全局唯一实例（与既有调用方式兼容） */
    static Logger& Instance() { return LazySingleton<Logger>::Instance(); }

    /** @brief 设置最低输出级别（低于此级别的日志将被忽略） */
    void SetLevel(LogLevel lv)  { m_level = lv;  }

    /**
     * @brief 设置日志基础路径（实时文件）
     * @param path 如 logs/aoi.log；归档为 logs/aoi.log.YYYYMMDD-HH
     * @note  空字符串则仅输出到 stdout
     */
    void SetPath(const std::string& path)
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_writer.SetBasePath(path);
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
        std::string ts = TimeUtil::Format(TimeUtil::UnixMs(), TIME_DEFAULT_FMT);
        const char* lvStr[] = {"DEBUG","INFO","WARN","ERROR","FATAL"};
        char line[2200];
        int n = snprintf(line, sizeof(line), "[%s][%s][%s] %s\n",
                         ts.c_str(), m_name.c_str(), lvStr[(int)lv], buf);
        std::lock_guard<std::mutex> lk(m_mu);
        fwrite(line, 1, n, stdout);
        fflush(stdout);
        if (m_writer.HasPath()) {
            m_writer.Write(line, static_cast<size_t>(n));
            m_writer.Flush();
        }
    }

private:
    /** @brief 私有构造（单例） */
    Logger() : m_level(LogLevel::DEBUG), m_name("Server") {}
    LogLevel       m_level;   /**< 最低输出级别 */
    std::string    m_name;    /**< 日志前缀中的服务器名 */
    std::string    m_path;    /**< 实时日志文件路径（空则仅 stdout） */
    LogFileWriter  m_writer;  /**< 双文件落盘写入器 */
    std::mutex     m_mu;      /**< 并发写日志互斥锁 */
};

// ============================================================
//  便捷宏 —— 简化调用
// ============================================================
/** @brief DEBUG 级日志宏（开发调试信息） */
#define LOG_DEBUG(fmt, ...) Logger::Instance().Log(LogLevel::DEBUG, fmt, ##__VA_ARGS__)
/** @brief INFO 级日志宏（常规运行信息） */
#define LOG_INFO(fmt, ...)  Logger::Instance().Log(LogLevel::INFO,  fmt, ##__VA_ARGS__)
/** @brief WARN 级日志宏（可恢复异常） */
#define LOG_WARN(fmt, ...)  Logger::Instance().Log(LogLevel::WARN,  fmt, ##__VA_ARGS__)
/** @brief ERR 级日志宏（错误但进程可继续） */
#define LOG_ERR(fmt, ...)   Logger::Instance().Log(LogLevel::ERR,   fmt, ##__VA_ARGS__)
/** @brief FATAL 级日志宏（致命错误） */
#define LOG_FATAL(fmt, ...) Logger::Instance().Log(LogLevel::FATAL, fmt, ##__VA_ARGS__)
