/**
 * @file    ExternServerConfig.h
 * @brief   外联服独立部署配置 —— 各服目录下 extern_*.xml 解析
 *
 * Logger/Global/Zone 进程在任意机器启动时读取各自目录中的 extern_*.xml，
 * 获取监听 ip/port、日志路径；Global/Zone 可选 Database；Global 可选 Http 双端。
 */

#pragma once

#include "XmlConfigUtil.h"

#include <cstdint>
#include <string>

#include <tinyxml2.h>

/**
 * @brief 外联服可选 MySQL 连接参数（与 config.xml Database 段同构）
 */
struct DatabaseConfig
{
    std::string host = "127.0.0.1"; /**< MySQL 主机 */
    int         port = 3306;         /**< MySQL 端口 */
    std::string user = "root";       /**< 用户名 */
    std::string pass;                /**< 密码 */
    std::string name = "rpg_game";   /**< 库名 */
    bool        configured = false;  /**< 存在 <Database> 节点时为 true，Init 须连库成功 */
};

/**
 * @brief GlobalServer HTTP 入站监听
 */
struct HttpListenConfig
{
    std::string listenIP = "0.0.0.0"; /**< 绑定 IP */
    uint16_t    port     = 0;          /**< 0 表示不启 HTTP 监听 */
};

/**
 * @brief GlobalServer HTTP 出站客户端
 */
struct HttpClientConfig
{
    std::string host;              /**< 远端主机 */
    uint16_t    port = 0;          /**< 远端端口，0 表示不连接 */
    bool        enabled = false;   /**< 是否启用出站客户端（与 port 独立，默认关） */
    bool        reconnect = false; /**< 断线指数退避重连 */
};

/**
 * @brief 外联服进程本地配置（由各服 extern_*.xml 填充）
 */
struct ExternServerConfig
{
    std::string listenIP   = "0.0.0.0"; /**< 游戏协议 TCP 监听 IP */
    uint16_t    listenPort = 0;          /**< 游戏协议 TCP 监听端口 */
    std::string logDir     = "logs";     /**< LoggerServer 远程日志落盘根目录 */
    std::string logPath    = "logs/global.log"; /**< Global/Zone 进程自身日志文件 */
    DatabaseConfig database;             /**< 可选 MySQL */
    HttpListenConfig httpListen;         /**< Global 专用：HTTP 入站 */
    HttpClientConfig httpClient;         /**< Global 专用：HTTP 出站 */
};

/**
 * @brief extern_*.xml 加载器
 */
class ExternServerConfigLoader
{
public:
    /**
     * @brief 加载外联服配置
     * @param path   LoggerServer/extern_logger.xml 等
     * @param cfg    [out] 解析结果
     * @param errOut 可选错误信息
     * @return 成功返回 true
     */
    static bool Load(const char* path, ExternServerConfig& cfg, std::string* errOut = nullptr)
    {
        tinyxml2::XMLDocument doc;
        if (!XmlConfig::loadDocument(path, doc, errOut))
            return false;

        tinyxml2::XMLElement* root = XmlConfig::requireRoot(doc, "ExternServer", errOut);
        if (!root)
            return false;

        cfg.listenIP   = "0.0.0.0";
        cfg.listenPort = 0;
        cfg.logDir     = "logs";
        cfg.logPath.clear();
        cfg.database = DatabaseConfig{};
        cfg.httpListen = HttpListenConfig{};
        cfg.httpClient = HttpClientConfig{};

        if (auto* listen = root->FirstChildElement("Listen"))
        {
            XmlConfig::readStrAttr(listen, "ip", cfg.listenIP);
            cfg.listenPort = (uint16_t)XmlConfig::readIntAttr(listen, "port", 0);
        }
        if (auto* logDir = root->FirstChildElement("LogDir"))
        {
            const std::string text = XmlConfig::readElementText(logDir);
            if (!text.empty())
                cfg.logDir = text;
        }
        if (auto* logPath = root->FirstChildElement("LogPath"))
        {
            const std::string text = XmlConfig::readElementText(logPath);
            if (!text.empty())
                cfg.logPath = text;
        }
        loadDatabase(root->FirstChildElement("Database"), cfg.database);
        if (auto* http = root->FirstChildElement("Http"))
            loadHttp(http, cfg.httpListen, cfg.httpClient);
        return true;
    }

private:
    static void loadDatabase(const tinyxml2::XMLElement* db, DatabaseConfig& out)
    {
        if (!db)
            return;
        out.configured = true;
        XmlConfig::readStrAttr(db, "host", out.host);
        out.port = XmlConfig::readIntAttr(db, "port", out.port);
        XmlConfig::readStrAttr(db, "user", out.user);
        XmlConfig::readStrAttr(db, "pass", out.pass);
        XmlConfig::readStrAttr(db, "name", out.name);
    }

    static bool parseBoolAttr(const tinyxml2::XMLElement* elem, const char* attrName,
                              bool fallback)
    {
        if (!elem || !attrName)
            return fallback;
        const char* v = elem->Attribute(attrName);
        if (!v || v[0] == '\0')
            return fallback;
        if (v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y')
            return true;
        if (v[0] == '0' || v[0] == 'f' || v[0] == 'F' || v[0] == 'n' || v[0] == 'N')
            return false;
        return fallback;
    }

    static bool parseReconnectAttr(const tinyxml2::XMLElement* elem, bool fallback)
    {
        return parseBoolAttr(elem, "reconnect", fallback);
    }

    static void loadHttp(const tinyxml2::XMLElement* http, HttpListenConfig& listen,
                         HttpClientConfig& client)
    {
        if (!http)
            return;
        if (auto* ln = http->FirstChildElement("Listen"))
        {
            XmlConfig::readStrAttr(ln, "ip", listen.listenIP);
            listen.port = (uint16_t)XmlConfig::readIntAttr(ln, "port", 0);
        }
        if (auto* cl = http->FirstChildElement("Client"))
        {
            XmlConfig::readStrAttr(cl, "host", client.host);
            client.port = (uint16_t)XmlConfig::readIntAttr(cl, "port", 0);
            client.enabled = parseBoolAttr(cl, "enabled", false);
            client.reconnect = parseReconnectAttr(cl, false);
        }
    }
};
