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
 *     <SessionServer port="9001"/>
 *     ...
 *     <LogPaths>
 *       <SuperServer>logs/super.log</SuperServer>   <!-- 实时文件；归档 super.log.YYYYMMDD-HH -->
 *       ...
 *     </LogPaths>
 *   </ServerConfig>
 * @endcode
 */

#pragma once
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
    int scenePort    = 9004;  /**< SceneServer 端口 */
    int gatewayPort  = 9005;  /**< GatewayServer 端口 */
    int loggerPort   = 9006;  /**< LoggerServer 端口 */
    int globalPort   = 9007;  /**< GlobalServer 端口 */
    int zonePort     = 9008;  /**< ZoneServer 端口 */

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
     * @param path config.xml 文件路径
     * @param cfg  [out] 填充的配置结构
     * @return 成功返回 true，文件不存在或格式错误返回 false
     */
    static bool Load(const char* path, ServerConfig& cfg)
    {
        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path) != tinyxml2::XML_SUCCESS) return false;
        auto* root = doc.RootElement();
        if (!root) return false;

        // ── 数据库配置 ──
        if (auto* db = root->FirstChildElement("Database"))
        {
            const char* v = nullptr;
            if ((v = db->Attribute("host")))  cfg.dbHost = v;
            if ((v = db->Attribute("port")))  cfg.dbPort = atoi(v);
            if ((v = db->Attribute("user")))  cfg.dbUser = v;
            if ((v = db->Attribute("pass")))  cfg.dbPass = v;
            if ((v = db->Attribute("name")))  cfg.dbName = v;
        }

        // ── SuperServer ──
        if (auto* ss = root->FirstChildElement("SuperServer"))
        {
            const char* v = nullptr;
            if ((v = ss->Attribute("ip")))   cfg.superIP   = v;
            if ((v = ss->Attribute("port"))) cfg.superPort = atoi(v);
        }

        // ── 各服务器端口（lambda 复用解析） ──
        auto loadPort = [&](const char* tag, int& port)
        {
            if (auto* e = root->FirstChildElement(tag))
                if (const char* v = e->Attribute("port")) port = atoi(v);
        };
        loadPort("SessionServer", cfg.sessionPort);
        loadPort("RecordServer",  cfg.recordPort);
        loadPort("AOIServer",     cfg.aoiPort);
        loadPort("SceneServer",   cfg.scenePort);
        loadPort("GatewayServer", cfg.gatewayPort);
        loadPort("LoggerServer",  cfg.loggerPort);
        loadPort("GlobalServer",  cfg.globalPort);
        loadPort("ZoneServer",    cfg.zonePort);

        // ── 日志路径 ──
        if (auto* lp = root->FirstChildElement("LogPaths"))
        {
            for (auto* e = lp->FirstChildElement(); e; e = e->NextSiblingElement())
                cfg.logPaths[e->Value()] = e->GetText() ? e->GetText() : "";
        }

        return true;
    }
};
