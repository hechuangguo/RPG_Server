/**
 * @file    NetTls.h
 * @brief   TLS 初始化与 TcpServer/TcpClient 装配辅助
 */

#pragma once

#include "TlsContext.h"
#include "TlsConfig.h"
#include "TcpServer.h"
#include "TcpClient.h"

#include <string>

#include <cstdio>

/** @brief 从配置初始化 TlsContext；失败写 stderr 并返回 false */
inline bool initNetTls(const TlsConfig& cfg)
{
    std::string err;
    if (!TlsContext::instance().init(cfg, &err))
    {
        if (!err.empty())
            fprintf(stderr, "TLS init failed: %s\n", err.c_str());
        return false;
    }
    return true;
}

/** @brief 对 TcpServer 启用 TLS（Start/Connect 前调用）
 *  @param requireClientCert false=玩家客户端口（9010/9005）；true=区内/注册 mTLS
 */
inline void wireTlsServer(TcpServer& server, bool requireClientCert = true)
{
    server.EnableTls(requireClientCert);
}

/** @brief 对 TcpClient 启用 TLS（Connect 前调用） */
inline void wireTlsClient(TcpClient& client)
{
    client.EnableTls();
}
