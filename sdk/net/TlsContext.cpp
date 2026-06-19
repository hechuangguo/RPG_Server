/**
 * @file    TlsContext.cpp
 * @brief   OpenSSL SSL_CTX 初始化与 mTLS 参数配置
 */

#include "TlsContext.h"
#include "../log/Logger.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cstdio>
#include <string>

namespace {

bool fileExists(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fclose(f);
    return true;
}

int tlsMinVersion(const std::string& ver)
{
    if (ver == "1.3")
        return TLS1_3_VERSION;
    return TLS1_2_VERSION;
}

void logOpenSslErrors(const char* prefix)
{
    unsigned long err = 0;
    while ((err = ERR_get_error()) != 0)
    {
        char buf[256];
        ERR_error_string_n(err, buf, sizeof(buf));
        LOG_WARN("%s: %s", prefix, buf);
    }
}

} // namespace

TlsContext& TlsContext::instance()
{
    static TlsContext ctx;
    return ctx;
}

TlsContext::~TlsContext()
{
    if (m_serverCtx)
    {
        SSL_CTX_free(m_serverCtx);
        m_serverCtx = nullptr;
    }
    if (m_clientCtx)
    {
        SSL_CTX_free(m_clientCtx);
        m_clientCtx = nullptr;
    }
}

bool TlsContext::init(const TlsConfig& cfg, std::string* errOut)
{
    m_config = cfg;
    m_enabled = cfg.enabled;

    if (m_serverCtx)
    {
        SSL_CTX_free(m_serverCtx);
        m_serverCtx = nullptr;
    }
    if (m_clientCtx)
    {
        SSL_CTX_free(m_clientCtx);
        m_clientCtx = nullptr;
    }

    if (!m_enabled)
    {
        LOG_INFO("TLS 未启用（Tls enabled=0）");
        return true;
    }

    if (!fileExists(cfg.certPath) || !fileExists(cfg.keyPath) || !fileExists(cfg.caPath))
    {
        if (errOut)
        {
            *errOut = "TLS 证书文件缺失，请执行 ./scripts/gen_tls_certs.sh（cert/key/ca）";
        }
        return false;
    }

    OPENSSL_init_ssl(0, nullptr);

    m_serverCtx = createCtx(true, errOut);
    if (!m_serverCtx)
        return false;

    m_clientCtx = createCtx(false, errOut);
    if (!m_clientCtx)
        return false;

    LOG_INFO("TLS 已启用 verifyPeer=%d cert=%s ca=%s min=%s",
             cfg.verifyPeer ? 1 : 0,
             cfg.certPath.c_str(),
             cfg.caPath.c_str(),
             cfg.minVersion.c_str());
    return true;
}

SSL_CTX* TlsContext::createCtx(bool serverSide, std::string* errOut)
{
    const SSL_METHOD* method = serverSide ? TLS_server_method() : TLS_client_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx)
    {
        if (errOut)
            *errOut = "SSL_CTX_new 失败";
        logOpenSslErrors("SSL_CTX_new");
        return nullptr;
    }

    SSL_CTX_set_min_proto_version(ctx, tlsMinVersion(m_config.minVersion));

    if (SSL_CTX_use_certificate_file(ctx, m_config.certPath.c_str(), SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, m_config.keyPath.c_str(), SSL_FILETYPE_PEM) <= 0 ||
        SSL_CTX_check_private_key(ctx) <= 0)
    {
        if (errOut)
            *errOut = "加载 cert/key 失败: " + m_config.certPath;
        logOpenSslErrors("cert/key");
        SSL_CTX_free(ctx);
        return nullptr;
    }

    if (SSL_CTX_load_verify_locations(ctx, m_config.caPath.c_str(), nullptr) <= 0)
    {
        if (errOut)
            *errOut = "加载 CA 失败: " + m_config.caPath;
        logOpenSslErrors("CA");
        SSL_CTX_free(ctx);
        return nullptr;
    }

    SSL_CTX_set_verify(ctx,
                       m_config.verifyPeer ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
                                           : SSL_VERIFY_NONE,
                       nullptr);

    if (!serverSide)
    {
        if (SSL_CTX_use_certificate_file(ctx, m_config.certPath.c_str(), SSL_FILETYPE_PEM) <= 0 ||
            SSL_CTX_use_PrivateKey_file(ctx, m_config.keyPath.c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            if (errOut)
                *errOut = "客户端 mTLS 证书加载失败";
            SSL_CTX_free(ctx);
            return nullptr;
        }
    }

    return ctx;
}

SSL* TlsContext::newServerSsl(int fd)
{
    if (!m_enabled || !m_serverCtx || fd < 0)
        return nullptr;
    SSL* ssl = SSL_new(m_serverCtx);
    if (!ssl)
        return nullptr;
    SSL_set_fd(ssl, fd);
    SSL_set_accept_state(ssl);
    return ssl;
}

SSL* TlsContext::newClientSsl(int fd)
{
    if (!m_enabled || !m_clientCtx || fd < 0)
        return nullptr;
    SSL* ssl = SSL_new(m_clientCtx);
    if (!ssl)
        return nullptr;
    SSL_set_fd(ssl, fd);
    SSL_set_connect_state(ssl);
    return ssl;
}
