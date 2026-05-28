/**
 * @file    LogFileWriter.h
 * @brief  双文件日志写入器：实时文件 + 按小时归档
 *
 * 给定基础路径 logs/aoi.log 时：
 * - 实时文件：logs/aoi.log（供 log.sh tail -F）
 * - 归档文件：logs/aoi.log.20260524-12（按小时切割）
 *
 * 文件轮转策略：
 * - 采用基于「小时槽」（hourSlot）的轮转机制，以 "YYYYMMDD-HH" 格式标识。
 * - 跨小时时归档文件自动切换；实时文件截断后仅保留当前小时内容。
 * - 归档文件使用追加模式（"a"），同一小时内的日志不会被覆盖。
 * - 实时文件在跨小时时使用截断模式（"w"），保证只保留当前小时的内容，
 *   方便运维通过 tail -F 实时监控。
 *
 * 缓冲区刷新时机：
 * - 本类使用 C stdio 的 FILE* 缓冲（默认全缓冲，通常 4KB/8KB）。
 * - 写入操作（Write）仅填充 stdio 缓冲区，不保证立即刷盘。
 * - 调用方需在适当时机调用 Flush() 强制刷新，建议：
 *   - 每帧主循环末尾调用一次
 *   - 关键错误日志写入后立即调用
 * - 析构函数 ~LogFileWriter() 中会调用 Close()，Close() 内 fclose 会隐式 fflush。
 *
 * 线程安全：
 * - **非线程安全**。若多线程写入，调用方需自行加锁。
 *
 * 性能特性：
 * - 使用 fwrite 而非 write()，利用 stdio 缓冲减少系统调用次数。
 * - 同时写入两个文件指针（实时+归档），开销为两次 fwrite 调用。
 * - EnsureOpen() 在同一小时内仅执行一次 fopen，后续 Write 路径无文件操作。
 */

#pragma once
#include "../time/TimeUtil.h"
#include <cstdio>
#include <string>

/**
 * @brief 双文件日志写入器
 *
 * 管理一个实时文件和一个按小时归档文件的同步写入。
 * 每次写入同时写到两个文件，归档文件跨小时自动切换。
 *
 * 使用示例：
 * @code
 *   LogFileWriter writer;
 *   writer.SetBasePath("logs/aoi.log");
 *   writer.Write(data, len);
 *   writer.Flush();  // 建议每帧调用
 * @endcode
 */
class LogFileWriter
{
public:
    /**
     * @brief 设置日志基础路径（同时关闭旧文件）
     * @param path 日志文件路径，如 "logs/aoi.log"
     * @note  路径变更会立即关闭当前打开的文件句柄
     */
    void SetBasePath(const std::string& path)
    {
        Close();
        m_basePath = path;
    }

    /** @brief 是否已设置基础路径（路径非空） */
    bool HasPath() const { return !m_basePath.empty(); }

    /**
     * @brief 写入日志数据到实时文件和归档文件
     * @param data 数据指针
     * @param len  数据长度（字节）
     *
     * 写入流程：
     * 1. 检查路径和数据有效性
     * 2. 调用 EnsureOpen() 检查是否需要切换小时槽或首次打开
     * 3. 分别向实时文件（m_fpLive）和归档文件（m_fpHourly）写入
     *
     * @note  写入受 stdio 缓冲控制，不一定立即落盘；需调用 Flush() 强制刷盘
     */
    void Write(const void* data, size_t len)
    {
        if (m_basePath.empty() || !data || len == 0) return;
        EnsureOpen();
        if (m_fpLive)   fwrite(data, 1, len, m_fpLive);
        if (m_fpHourly) fwrite(data, 1, len, m_fpHourly);
    }

    /**
     * @brief 刷新所有文件缓冲区到磁盘
     *
     * 对实时文件和归档文件分别调用 fflush()。
     * 建议在主循环每帧末尾调用，确保日志及时落盘（进程崩溃时不丢失数据）。
     */
    void Flush()
    {
        if (m_fpLive)   fflush(m_fpLive);
        if (m_fpHourly) fflush(m_fpHourly);
    }

    /** @brief 析构时自动关闭所有文件 */
    ~LogFileWriter() { Close(); }

private:
    /**
     * @brief 关闭所有文件句柄并重置状态
     *
     * fflush + fclose 确保缓冲区数据刷盘后释放句柄。
     * 同时清除小时槽记录，使下次 Write 时重新打开。
     */
    void Close()
    {
        if (m_fpLive)   { fclose(m_fpLive);   m_fpLive   = nullptr; }
        if (m_fpHourly) { fclose(m_fpHourly); m_fpHourly = nullptr; }
        m_hourSlot.clear();
    }

    /**
     * @brief 确保文件句柄已打开且当前小时槽正确
     *
     * 轮转判断逻辑：
     * 1. 根据当前时间计算 "YYYYMMDD-HH" 格式的小时槽
     * 2. 若小时槽未变且两个文件句柄均有效 → 无需操作（快速路径）
     * 3. 若小时槽变化（跨小时）→ 关闭旧句柄，打开新句柄：
     *    - 实时文件：使用 "w" 模式（截断），保证仅保留当前小时内容
     *    - 归档文件：使用 "a" 模式（追加），追加到对应小时的归档文件
     * 4. 首次写入（m_hourSlot 为空）→ 实时文件使用 "a" 模式，不截断已有内容
     *
     * @note  此方法在每次 Write() 时调用，但实际 fopen 仅在跨小时或首次时执行
     */
    void EnsureOpen()
    {
        const std::string slot =
            TimeUtil::Format(TimeUtil::UnixMs(), "%Y%m%d-%H");
        if (slot == m_hourSlot && m_fpLive && m_fpHourly) return;
        const bool hourChanged =
            !m_hourSlot.empty() && slot != m_hourSlot;
        if (m_fpHourly) { fclose(m_fpHourly); m_fpHourly = nullptr; }
        if (m_fpLive)   { fclose(m_fpLive);   m_fpLive   = nullptr; }
        const std::string hourlyPath = m_basePath + "." + slot;
        m_fpLive   = fopen(m_basePath.c_str(), hourChanged ? "w" : "a");
        m_fpHourly = fopen(hourlyPath.c_str(), "a");
        m_hourSlot = slot;
    }
    std::string m_basePath;     /**< 日志基础路径，如 "logs/aoi.log" */
    std::string m_hourSlot;     /**< 当前小时槽标识 "YYYYMMDD-HH"，用于检测跨小时 */
    FILE*       m_fpLive   = nullptr; /**< 实时日志文件句柄（tail -F 监控用） */
    FILE*       m_fpHourly = nullptr; /**< 按小时归档文件句柄 */
};
