/**
 * @file    TlsContext.h
 * @brief   进程级 OpenSSL SSL_CTX 管理（服务端/客户端 mTLS）
 */

#pragma once

#include "TlsConfig.h"

struct ssl_st;
typedef struct ssl_st SSL;
struct ssl_ctx_st;
typedef struct ssl_ctx_st SSL_CTX;

/**
 * @brief 全局 TLS 上下文单例（每进程一份 server_ctx + client_ctx）
 */
class TlsContext
{
public:
    /** @brief 获取单例 */
    static TlsContext& instance();

    /**
     * @brief 按配置初始化 OpenSSL 并创建 SSL_CTX
     * @param cfg TLS 配置
     * @param errOut 失败原因
     * @return enabled=false 时直接 true；enabled=true 且证书加载成功时 true
     */
    bool init(const TlsConfig& cfg, std::string* errOut = nullptr);

    /** @brief TLS 是否已启用 */
    bool enabled() const { return m_enabled; }

    /** @brief 服务端 SSL_CTX（accept）；未启用时 nullptr */
    SSL_CTX* serverCtx() const { return m_serverCtx; }

    /** @brief 客户端 SSL_CTX（connect）；未启用时 nullptr */
    SSL_CTX* clientCtx() const { return m_clientCtx; }

    /** @brief 当前配置快照 */
    const TlsConfig& config() const { return m_config; }

    /** @brief 为已 accept/connect 的 fd 创建服务端 SSL 对象 */
    SSL* newServerSsl(int fd);

    /** @brief 为已 connect 的 fd 创建客户端 SSL 对象 */
    SSL* newClientSsl(int fd);

private:
    TlsContext() = default;
    ~TlsContext();
    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

    SSL_CTX* createCtx(bool serverSide, std::string* errOut);

    bool        m_enabled   = false;
    TlsConfig   m_config{};
    SSL_CTX*    m_serverCtx = nullptr;
    SSL_CTX*    m_clientCtx = nullptr;
};
