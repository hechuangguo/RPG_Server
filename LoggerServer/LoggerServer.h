/**
 * @file    LoggerServer.h
 * @brief  日志服务器 —— 接收各服务器日志写入请求，统一落盘
 *
 * ## 职责
 * - 接收来自各服务器的 LOG_WRITE_REQ 消息
 * - 按服务器类型和日期分文件写入（如 session_20260523.log）
 * - 每小时自动日志切割（关闭旧句柄，下次写入创建新文件）
 *
 * ## 日志文件命名规则
 * @code
 *   {ServerType}_{YYYYMMDD}.log
 *   例如：scene_20260523.log、gateway_20260523.log
 * @endcode
 *
 * ## 依赖关系
 * - 依赖 SuperServer + SessionServer
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <fstream>
#include <string>
#include <vector>
#include <ctime>

/**
 * @brief 远程日志写入请求结构
 *
 * 后跟 logLen 字节的日志文本（纯文本，不含换行）。
 */
#pragma pack(push, 1)
struct Msg_Log_WriteReq
{
    uint8_t  serverType;  /**< 来源服务器类型（SubServerType 枚举值） */
    uint8_t  level;       /**< 日志级别：0=DEBUG 1=INFO 2=WARN 3=ERR 4=FATAL */
    uint32_t logLen;      /**< 日志文本长度（字节）—— 后跟 logLen 字节数据 */
};
#pragma pack(pop)

/**
 * @brief LoggerServer 核心类
 *
 * 单进程运行，维护按文件名索引的 FILE* 句柄表。
 */
class LoggerServer : public INetCallback
{
public:
    LoggerServer() : m_server(this), m_superClient(this), m_sessionClient(this) {}

    /**
     * @brief 初始化 LoggerServer
     * @param ip         监听 IP
     * @param port       监听端口
     * @param superIP    SuperServer IP
     * @param superPort  SuperServer 端口
     * @param sessionIP  SessionServer IP
     * @param sessionPort SessionServer 端口
     * @param logDir     日志输出目录
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port,
              const std::string& superIP, uint16_t superPort,
              const std::string& sessionIP, uint16_t sessionPort,
              const std::string& logDir)
    {
        Logger::Instance().SetServerName("LoggerServer");
        m_logDir = logDir;
        if (!m_server.Start(ip, port)) { LOG_FATAL("LoggerServer start failed"); return false; }
        m_superClient.Connect(superIP, superPort);
        m_sessionClient.Connect(sessionIP, sessionPort);
        RegisterHandlers();
        TimerMgr::Instance().Register(500,   0,     [this]{ RegisterToSuper(); });
        TimerMgr::Instance().Register(10000, 10000, [this]{ SendHeartbeat(); });
        // 每小时刷新日志文件句柄（日志切割）
        TimerMgr::Instance().Register(3600000, 3600000, [this]{ FlushAll(); });
        LOG_INFO("LoggerServer started on %s:%d", ip.c_str(), port);
        return true;
    }

    /** @brief 主循环 */
    void Run()
    {
        while (true)
        {
            m_superClient.Poll(0);
            m_sessionClient.Poll(0);
            m_server.Poll(10);
            TimerMgr::Instance().Update();
        }
    }

    void OnConnect(ConnID id)    override {}
    void OnDisconnect(ConnID id) override {}
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::LOG_WRITE_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnWriteLog(c, d, l); });
    }

    /**
     * @brief 处理远程日志写入请求
     *
     * 解析 Msg_Log_WriteReq 头部，提取日志文本，写入对应文件。
     * 文件按需打开（首次写入某服务器的日志时）。
     */
    void OnWriteLog(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_Log_WriteReq)) return;
        const auto* req = reinterpret_cast<const Msg_Log_WriteReq*>(data);
        uint32_t logLen = req->logLen;
        if ((uint32_t)len < sizeof(Msg_Log_WriteReq) + logLen) return;
        const char* logText = data + sizeof(Msg_Log_WriteReq);

        std::string fname = GetLogFileName((SubServerType)req->serverType);
        auto it = m_files.find(fname);
        if (it == m_files.end())
        {
            std::string path = m_logDir + "/" + fname;
            FILE* fp = fopen(path.c_str(), "a");
            if (!fp) { LOG_ERR("Cannot open log file: %s", path.c_str()); return; }
            m_files[fname] = fp;
            it = m_files.find(fname);
        }
        fwrite(logText, 1, logLen, it->second);
        fputc('\n', it->second);
        fflush(it->second);  /**< 每条日志立即刷新 */
    }

    /**
     * @brief 生成日志文件名
     * @param type 服务器类型
     * @return 如 "scene_20260523.log"
     */
    std::string GetLogFileName(SubServerType type)
    {
        time_t now = time(nullptr);
        struct tm t{}; localtime_r(&now, &t);
        char ts[16]; snprintf(ts, sizeof(ts), "%04d%02d%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday);
        static const char* names[] = {"unknown","session","record","aoi","scene","gateway","logger","global","zone"};
        int idx = (int)type;
        if (idx < 0 || idx >= 9) idx = 0;
        char buf[64]; snprintf(buf, sizeof(buf), "%s_%s.log", names[idx], ts);
        return buf;
    }

    /**
     * @brief 日志切割（每小时执行）
     *
     * 关闭所有文件句柄，下次写入时重新按日期创建文件名。
     */
    void FlushAll()
    {
        for (auto& [n, fp] : m_files) { fflush(fp); fclose(fp); }
        m_files.clear();
        LOG_INFO("LoggerServer: log files rotated.");
    }

    void RegisterToSuper()
    {
        Msg_S2S_Register reg{};
        reg.serverType = (uint8_t)SubServerType::LOGGER;
        reg.serverID   = 1;
        strncpy(reg.ip, "127.0.0.1", sizeof(reg.ip));
        reg.port       = 9006;
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                               reinterpret_cast<char*>(&reg), sizeof(reg));
    }

    void SendHeartbeat()
    {
        Msg_S2S_Heartbeat hb{}; hb.seq = ++m_hbSeq; hb.timestamp = TimerMgr::NowMs();
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                               reinterpret_cast<char*>(&hb), sizeof(hb));
    }

    TcpServer  m_server;         /**< 内部连接监听 */
    TcpClient  m_superClient;    /**< 到 SuperServer 的连接 */
    TcpClient  m_sessionClient;  /**< 到 SessionServer 的连接 */
    uint32_t   m_hbSeq = 0;      /**< 心跳序列号 */
    std::string m_logDir;        /**< 日志输出根目录 */

    /** @brief 文件句柄表：文件名 → FILE*（按需打开） */
    std::unordered_map<std::string, FILE*> m_files;
};
