/**
 * @file    TlsConfig.h
 * @brief   TLS 传输层配置（证书路径、mTLS 校验开关）
 */

#pragma once

#include "../util/XmlConfigUtil.h"

#include <cstdint>
#include <string>

#include <tinyxml2.h>

/**
 * @brief 进程级 TLS 配置（config.xml / extern_*.xml 的 Tls 段）
 */
struct TlsConfig
{
    bool        enabled    = false; /**< true 时全部 TcpServer/TcpClient 走 TLS */
    std::string certPath   = "config/tls/server.crt"; /**< 本进程服务端/客户端证书 */
    std::string keyPath    = "config/tls/server.key"; /**< 私钥 */
    std::string caPath     = "config/tls/ca.crt";     /**< CA（校验对端 + mTLS） */
    bool        verifyPeer = true;  /**< 是否校验对端证书（区内 mTLS 应为 true） */
    std::string minVersion = "1.2"; /**< 最低 TLS 版本（1.2 / 1.3） */
};

/**
 * @brief 从 XML 节点解析 TlsConfig
 * @param tlsNode Tls 元素；nullptr 时 cfg 保持默认（enabled=false）
 */
inline void loadTlsConfigFromXml(const tinyxml2::XMLElement* tlsNode, TlsConfig& cfg)
{
    if (!tlsNode)
        return;

    cfg.enabled = XmlConfig::readIntAttr(tlsNode, "enabled", cfg.enabled ? 1 : 0) != 0;
    XmlConfig::readStrAttr(tlsNode, "cert", cfg.certPath);
    XmlConfig::readStrAttr(tlsNode, "key", cfg.keyPath);
    XmlConfig::readStrAttr(tlsNode, "ca", cfg.caPath);
    cfg.verifyPeer = XmlConfig::readIntAttr(tlsNode, "verifyPeer", cfg.verifyPeer ? 1 : 0) != 0;
    XmlConfig::readStrAttr(tlsNode, "minVersion", cfg.minVersion);
}
