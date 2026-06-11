/**
 * @file    LoggerServer.cpp
 * @brief   LoggerServer 实现（外联独立部署，无 Super 注册）
 */

#include "LoggerServer.h"

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
        LOG_FATAL("LoggerServer start failed");
        return false;
    }
    RegisterHandlers();
    LOG_INFO("LoggerServer started on %s:%d logDir=%s", ip.c_str(), port, logDir.c_str());
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
    MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
}

void LoggerServer::RegisterHandlers()
{
    auto& d = MsgDispatcher::Instance();
    d.Register((uint16_t)InternalMsgID::LOG_WRITE_REQ,
               [this](uint32_t c, const char* d, uint16_t l) { OnWriteLog(c, d, l); });
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
