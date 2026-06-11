/**
 * @file    ConfigLoader.h
 * @brief  全局配置文件解析器 —— config.xml → ServerConfig
 *
 * 使用 tinyxml2 解析 XML 配置，将数据库连接信息、各服务器端口、
 * 日志路径等填入 ServerConfig 结构体。
 *
 * XML 结构参考：
 * @code
 *   <ServerConfig>
 *     <Database host=".." port=".." user=".." pass=".." name=".."/>
 *     <SuperServer ip=".." port=".."/>
 *     <!-- Session/Record/AOI/Scene/Gateway 端口已迁移至 DB 的 ServerList -->
 *     <!-- Logger/Global/Zone 外联地址见根目录 loginserverlist.xml -->
 *     <LogPaths>
 *       <SuperServer>logs/super.log</SuperServer>
 *       ...
 *     </LogPaths>
 *   </ServerConfig>
 * @endcode
 */

#pragma once

#include "XmlConfigUtil.h"

#include <string>
#include <unordered_map>

#include <tinyxml2.h>

/**
 * @brief 全局服务器配置结构
 *
 * 所有服务器启动时通过 ConfigLoader::Load() 填充此结构。
 */
struct ServerConfig
{
    // ── 数据库 ──
    std::string dbHost     = "127.0.0.1";  /**< MySQL 主机 */
    int         dbPort     = 3306;         /**< MySQL 端口 */
    std::string dbUser     = "root";       /**< MySQL 用户名 */
    std::string dbPass     = "";           /**< MySQL 密码 */
    std::string dbName     = "rpg_game";   /**< MySQL 数据库名 */
    // ── SuperServer ──
    std::string superIP    = "127.0.0.1";  /**< SuperServer 监听 IP */
    int         superPort  = 9000;         /**< SuperServer 监听端口 */
    // ── 各服务器内部通信端口 ──
    int sessionPort  = 9001;  /**< SessionServer 端口 */
    int recordPort   = 9002;  /**< RecordServer 端口 */
    int aoiPort      = 9003;  /**< AOIServer 端口 */
    int scenePort    = 9004;  /**< SceneServer 端口（区内 ServerList；保留默认值兜底） */
    int gatewayPort  = 9005;  /**< GatewayServer 端口（区内 ServerList；保留默认值兜底） */
    /** @brief 日志输出路径映射：服务器名称 → 日志文件路径 */
    std::unordered_map<std::string, std::string> logPaths;
};

/**
 * @brief 配置文件加载器（静态工具类）
 */
class ConfigLoader
{
public:
    /**
     * @brief 从 XML 文件加载全局配置
     * @param path   config.xml 文件路径
     * @param cfg    [out] 填充的配置结构
     * @param errOut 可选；失败时写入可读错误信息
     * @return 成功返回 true
     */
    static bool Load(const char* path, ServerConfig& cfg, std::string* errOut = nullptr)
    {
        tinyxml2::XMLDocument doc;
        if (!XmlConfig::loadDocument(path, doc, errOut))
            return false;
        tinyxml2::XMLElement* root = XmlConfig::requireRoot(doc, "ServerConfig", errOut);
        if (!root)
            return false;
        cfg.logPaths.clear();
        if (auto* db = root->FirstChildElement("Database"))
        {
            XmlConfig::readStrAttr(db, "host", cfg.dbHost);
            cfg.dbPort = XmlConfig::readIntAttr(db, "port", cfg.dbPort);
            XmlConfig::readStrAttr(db, "user", cfg.dbUser);
            XmlConfig::readStrAttr(db, "pass", cfg.dbPass);
            XmlConfig::readStrAttr(db, "name", cfg.dbName);
        }
        if (auto* ss = root->FirstChildElement("SuperServer"))
        {
            XmlConfig::readStrAttr(ss, "ip", cfg.superIP);
            cfg.superPort = XmlConfig::readIntAttr(ss, "port", cfg.superPort);
        }
        // Session/Record/AOI/Scene/Gateway 端口已迁移至 DB 的 ServerList。
        // Logger/Global/Zone 外联地址见 loginserverlist.xml，不在此加载。
        if (auto* lp = root->FirstChildElement("LogPaths"))
        {
            for (auto* e = lp->FirstChildElement(); e; e = e->NextSiblingElement())
            {
                const std::string text = XmlConfig::readElementText(e);
                if (!text.empty())
                    cfg.logPaths[e->Name()] = text;
            }
        }
        return true;
    }
};
