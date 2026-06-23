/**
 * @file    ProductionConfigValidator.cpp
 */
#include "ProductionConfigValidator.h"
#include "../log/Logger.h"

ProductionConfigCheckResult validateProductionConfig(const TlsConfig& tls,
                                                     const std::string& dbPass,
                                                     bool enforceProduction)
{
    ProductionConfigCheckResult result;
    if (!enforceProduction)
        return result;
    if (!tls.enabled)
    {
        result.ok = false;
        result.message = "生产环境禁止 TLS enabled=0";
        return result;
    }
    if (!tls.verifyPeer)
    {
        result.ok = false;
        result.message = "生产环境禁止 TLS verifyPeer=0";
        return result;
    }
    if (dbPass == "rpg_table" || dbPass == "root" || dbPass.empty())
    {
        result.ok = false;
        result.message = "生产环境禁止使用默认或空数据库密码";
        return result;
    }
    return result;
}
