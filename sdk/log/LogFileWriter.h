/**
 * @file    LogFileWriter.h
 * @brief  双文件日志写入：实时文件 + 按小时归档
 *
 * 给定基础路径 logs/aoi.log 时：
 * - 实时文件：logs/aoi.log（供 log.sh tail -F）
 * - 归档文件：logs/aoi.log.20260524-12（按小时切割）
 *
 * 跨小时时归档文件自动切换；实时文件截断后仅保留当前小时内容。
 */

#pragma once
#include "../time/TimeUtil.h"
#include <cstdio>
#include <string>

class LogFileWriter
{
public:
    void SetBasePath(const std::string& path)
    {
        Close();
        m_basePath = path;
    }

    bool HasPath() const { return !m_basePath.empty(); }

    void Write(const void* data, size_t len)
    {
        if (m_basePath.empty() || !data || len == 0) return;
        EnsureOpen();
        if (m_fpLive)   fwrite(data, 1, len, m_fpLive);
        if (m_fpHourly) fwrite(data, 1, len, m_fpHourly);
    }

    void Flush()
    {
        if (m_fpLive)   fflush(m_fpLive);
        if (m_fpHourly) fflush(m_fpHourly);
    }

    ~LogFileWriter() { Close(); }

private:
    void Close()
    {
        if (m_fpLive)   { fclose(m_fpLive);   m_fpLive   = nullptr; }
        if (m_fpHourly) { fclose(m_fpHourly); m_fpHourly = nullptr; }
        m_hourSlot.clear();
    }

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

    std::string m_basePath;
    std::string m_hourSlot;
    FILE*       m_fpLive   = nullptr;
    FILE*       m_fpHourly = nullptr;
};
