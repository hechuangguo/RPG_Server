/**
 * @file    ProductionConfigValidator.h
 * @brief   生产环境配置强校验（TLS/DB 明文密码等）
 */
#pragma once
#include "../net/TlsConfig.h"
#include <string>

struct ProductionConfigCheckResult
{
    bool ok = true;
    std::string message;
};

ProductionConfigCheckResult validateProductionConfig(const TlsConfig& tls,
                                                     const std::string& dbPass,
                                                     bool enforceProduction);
