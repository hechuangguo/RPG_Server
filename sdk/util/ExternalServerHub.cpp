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
}  // namespace

void ExternalServerHub::configure(const LoginServerList& list, bool wantLogger,
                                  bool wantGlobal, bool wantZone)
{
    m_wantLogger = wantLogger;
    m_wantGlobal = wantGlobal;
    m_wantZone   = wantZone;

    applyEntry(m_logger, wantLogger, list, SubServerType::LOGGER);
    applyEntry(m_global, wantGlobal, list, SubServerType::GLOBAL);
    applyEntry(m_zone, wantZone, list, SubServerType::ZONE);
}

void ExternalServerHub::connectAll()
{
    if (m_wantLogger)
        m_logger.connectIfConfigured();
    if (m_wantGlobal)
        m_global.connectIfConfigured();
    if (m_wantZone)
        m_zone.connectIfConfigured();
}

void ExternalServerHub::poll()
{
    if (m_wantLogger)
        m_logger.poll();
    if (m_wantGlobal)
        m_global.poll();
    if (m_wantZone)
        m_zone.poll();
}

void ExternalServerHub::tickReconnect()
{
    const uint64_t now = TimerMgr::NowMs();
    if (m_wantLogger)
        m_logger.tickReconnect(now);
    if (m_wantGlobal)
        m_global.tickReconnect(now);
    if (m_wantZone)
        m_zone.tickReconnect(now);
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
