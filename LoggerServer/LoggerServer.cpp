/**
 * @file LoggerServer.cpp
 * @brief LoggerServer 非内联方法实现。
 */

#include "LoggerServer.h"

LoggerServer::LoggerServer()
    : m_server(this), m_superClient(this), m_sessionClient(this)
{
}

bool LoggerServer::Init(const std::string& ip, uint16_t port,
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
    LOG_INFO("LoggerServer started on %s:%d", ip.c_str(), port);
    return true;
}

void LoggerServer::Run()
{
    while (true)
    {
        m_superClient.Poll(0);
        m_sessionClient.Poll(0);
        m_server.Poll(10);
        TimerMgr::Instance().Update();
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
        [this](uint32_t c, const char* d, uint16_t l){ OnWriteLog(c, d, l); });
}

void LoggerServer::OnWriteLog(ConnID /*fromConn*/, const char* data, uint16_t len)
{
    if (len < sizeof(Msg_Log_WriteReq)) return;
    const auto* req = reinterpret_cast<const Msg_Log_WriteReq*>(data);
    uint32_t logLen = req->logLen;
    if ((uint32_t)len < sizeof(Msg_Log_WriteReq) + logLen) return;
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
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= 9) idx = 0;
    return names[idx];
}

LogFileWriter& LoggerServer::GetWriter(SubServerType type)
{
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= 9) idx = 0;
    auto it = m_writers.find(idx);
    if (it != m_writers.end()) return it->second;

    std::string path = m_logDir + "/" + ServerLogBaseName(type);
    LogFileWriter writer;
    writer.SetBasePath(path);
    auto [ins, _] = m_writers.emplace(idx, std::move(writer));
    return ins->second;
}

void LoggerServer::RegisterToSuper()
{
    Msg_S2S_Register reg{};
    reg.serverType = (uint8_t)SubServerType::LOGGER;
    reg.serverID   = 1;
    copyToWire(reg.ip, sizeof(reg.ip), "127.0.0.1");
    reg.port       = 9006;
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                          reinterpret_cast<char*>(&reg), sizeof(reg));
}

void LoggerServer::SendHeartbeat()
{
    Msg_S2S_Heartbeat hb{}; hb.seq = ++m_hbSeq; hb.timestamp = TimerMgr::NowMs();
    m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                          reinterpret_cast<char*>(&hb), sizeof(hb));
}
