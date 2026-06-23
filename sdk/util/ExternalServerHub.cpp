/**
 * @file    ExternalServerHub.cpp
 * @brief   外联服连接聚合实现
 */

#include "ExternalServerHub.h"

#include "../timer/TimerMgr.h"

namespace
{
void applyEntry(ExternalServerConnector& conn, bool want,
                const LoginServerList& list, SubServerType type)
{
    if (!want)
        return;
    if (const ExternalServerEntry* e = list.find(type))
        conn.setEntry(*e);
}
} // namespace

void ExternalServerHub::configure(const LoginServerList& list, bool wantLogger,
                                  bool wantGlobal, bool wantZone, bool wantLogin)
{
    m_wantLogger = wantLogger;
    m_wantGlobal = wantGlobal;
    m_wantZone   = wantZone;
    m_wantLogin  = wantLogin;

    applyEntry(m_logger, wantLogger, list, SubServerType::LOGGER);
    applyEntry(m_global, wantGlobal, list, SubServerType::GLOBAL);
    applyEntry(m_zone, wantZone, list, SubServerType::ZONE);
    applyEntry(m_login, wantLogin, list, SubServerType::LOGIN);
}

void ExternalServerHub::connectAll()
{
    if (m_wantLogger)
        m_logger.connectIfConfigured();
    if (m_wantGlobal)
        m_global.connectIfConfigured();
    if (m_wantZone)
        m_zone.connectIfConfigured();
    if (m_wantLogin)
        m_login.connectIfConfigured();
}

void ExternalServerHub::poll()
{
    if (m_wantLogger)
        m_logger.poll();
    if (m_wantGlobal)
        m_global.poll();
    if (m_wantZone)
        m_zone.poll();
    if (m_wantLogin)
        m_login.poll();
}

void ExternalServerHub::tickReconnect(uint64_t nowMs, SubServerType skipType)
{
    if (m_wantLogger)
        m_logger.tickReconnect(nowMs);
    if (m_wantGlobal)
        m_global.tickReconnect(nowMs);
    if (m_wantZone)
        m_zone.tickReconnect(nowMs);
    /** 仅在连接仍存活时暂缓 Login 重连；已断线则允许重连并交由上层清理在途校验 */
    if (m_wantLogin)
    {
        const bool skipLoginReconnect =
            skipType == SubServerType::LOGIN && m_login.isConnected();
        if (!skipLoginReconnect)
            m_login.tickReconnect(nowMs);
    }
}

ExternalServerConnector* ExternalServerHub::connector(SubServerType type)
{
    switch (type)
    {
    case SubServerType::LOGGER:
        return m_wantLogger ? &m_logger : nullptr;
    case SubServerType::GLOBAL:
        return m_wantGlobal ? &m_global : nullptr;
    case SubServerType::ZONE:
        return m_wantZone ? &m_zone : nullptr;
    case SubServerType::LOGIN:
        return m_wantLogin ? &m_login : nullptr;
    default:
        return nullptr;
    }
}

TcpClient* ExternalServerHub::client(SubServerType type)
{
    ExternalServerConnector* c = connector(type);
    if (!c || !c->isConfigured())
        return nullptr;
    return &c->client();
}
