/**
 * @file    LoggerServer.cpp
 * @brief   LoggerServer 实现（外联独立部署，无 Super 注册）
 */

#include "LoggerServer.h"
#include "LoggerInternMsgRegister.h"
#include "../sdk/net/MsgIngress.h"

LoggerServer::LoggerServer()
    : m_server(this)
{
}

bool LoggerServer::Init(const std::string& ip, uint16_t port, const std::string& logDir)
{
    Logger::Instance().SetServerName("LoggerServer");
    m_logDir = logDir;
    if (!m_server.Start(ip, port))
    {
        LOG_FATAL("日志服启动失败");
        return false;
    }
    RegisterHandlers();
    LOG_INFO("日志服启动完成: %s:%d logDir=%s", ip.c_str(), port, logDir.c_str());
    return true;
}

void LoggerServer::Run()
{
    while (true)
    {
        m_server.Poll(10);
    }
}

void LoggerServer::OnConnect(ConnID /*id*/)
{
}

void LoggerServer::OnDisconnect(ConnID /*id*/)
{
}

void LoggerServer::OnMessage(ConnID id, uint8_t module, uint8_t sub,
                             const char* data, uint16_t len)
{
    MsgIngress::dispatchInternal(id, module, sub, data, len);
}

void LoggerServer::RegisterHandlers()
{
    LoggerInternMsgRegister(*this);
}

void LoggerServer::OnWriteLog(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_Log_WriteReq))
        return;
    const auto* req = reinterpret_cast<const Msg_Log_WriteReq*>(data);
    uint32_t logLen = req->logLen;
    if ((uint32_t)len < sizeof(Msg_Log_WriteReq) + logLen)
        return;
    const char* logText = data + sizeof(Msg_Log_WriteReq);

    auto& writer = GetWriter((SubServerType)req->serverType);
    writer.Write(logText, logLen);
    writer.Write("\n", 1);
    writer.Flush();
}

const char* LoggerServer::ServerLogBaseName(SubServerType type)
{
    static const char* names[] = {
        "unknown.log", "session.log", "record.log", "aoi.log",
        "scene.log", "gateway.log", "logger.log", "global.log", "zone.log"
    };
    const int idx = (int)type;
    if (idx >= 0 && idx < (int)(sizeof(names) / sizeof(names[0])))
        return names[idx];
    return "unknown.log";
}

LogFileWriter& LoggerServer::GetWriter(SubServerType type)
{
    const int idx = (int)type;
    auto it = m_writers.find(idx);
    if (it != m_writers.end())
        return it->second;

    std::string path = m_logDir + "/" + ServerLogBaseName(type);
    LogFileWriter writer;
    writer.SetBasePath(path);
    auto [ins, _] = m_writers.emplace(idx, std::move(writer));
    return ins->second;
}
