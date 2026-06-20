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
    if (m_serverOneWayCtx)
    {
        SSL_CTX_free(m_serverOneWayCtx);
        m_serverOneWayCtx = nullptr;
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
    if (m_serverOneWayCtx)
    {
        SSL_CTX_free(m_serverOneWayCtx);
        m_serverOneWayCtx = nullptr;
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

    m_serverCtx = createCtx(true, cfg.verifyPeer, errOut);
    if (!m_serverCtx)
        return false;

    m_serverOneWayCtx = createCtx(true, false, errOut);
    if (!m_serverOneWayCtx)
        return false;

    m_clientCtx = createCtx(false, cfg.verifyPeer, errOut);
    if (!m_clientCtx)
        return false;

    LOG_INFO("TLS 已启用 verifyPeer=%d cert=%s ca=%s min=%s",
             cfg.verifyPeer ? 1 : 0,
             cfg.certPath.c_str(),
             cfg.caPath.c_str(),
             cfg.minVersion.c_str());
    return true;
}

SSL_CTX* TlsContext::createCtx(bool serverSide, bool verifyPeer, std::string* errOut)
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

    const long sslOpts = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
    SSL_CTX_set_options(ctx, sslOpts);

    const std::string& ciphers = m_config.cipherSuites.empty()
        ? "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
          "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384"
        : m_config.cipherSuites;
    if (SSL_CTX_set_cipher_list(ctx, ciphers.c_str()) <= 0)
    {
        if (errOut)
            *errOut = "TLS cipherSuites 配置无效: " + ciphers;
        logOpenSslErrors("cipher_list");
        SSL_CTX_free(ctx);
        return nullptr;
    }

#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    const std::string& tls13 = m_config.tls13CipherSuites.empty()
        ? "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384"
        : m_config.tls13CipherSuites;
    if (SSL_CTX_set_ciphersuites(ctx, tls13.c_str()) <= 0)
    {
        if (errOut)
            *errOut = "TLS tls13CipherSuites 配置无效: " + tls13;
        logOpenSslErrors("ciphersuites");
        SSL_CTX_free(ctx);
        return nullptr;
    }
#endif

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
                       verifyPeer ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
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

SSL* TlsContext::newServerSsl(int fd, bool requireClientCert)
{
    if (!m_enabled || fd < 0)
        return nullptr;
    SSL_CTX* ctx = requireClientCert ? m_serverCtx : m_serverOneWayCtx;
    if (!ctx)
        return nullptr;
    SSL* ssl = SSL_new(ctx);
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
