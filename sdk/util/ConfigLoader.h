#pragma once
#include <string>
#include <unordered_map>
#include <tinyxml2.h>

// ============================================================
//  全局配置解析（config.xml）
// ============================================================
struct ServerConfig
{
    // 数据库配置
    std::string dbHost     = "127.0.0.1";
    int         dbPort     = 3306;
    std::string dbUser     = "root";
    std::string dbPass     = "";
    std::string dbName     = "rpg_game";

    // SuperServer
    std::string superIP    = "127.0.0.1";
    int         superPort  = 9000;

    // 各服务器监听端口
    int sessionPort  = 9001;
    int recordPort   = 9002;
    int aoiPort      = 9003;
    int scenePort    = 9004;
    int gatewayPort  = 9005;
    int loggerPort   = 9006;
    int globalPort   = 9007;
    int zonePort     = 9008;

    // 日志路径（key=服务器名，value=路径）
    std::unordered_map<std::string, std::string> logPaths;
};

class ConfigLoader
{
public:
    static bool Load(const char* path, ServerConfig& cfg)
    {
        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path) != tinyxml2::XML_SUCCESS) return false;
        auto* root = doc.RootElement();
        if (!root) return false;

        // 数据库
        if (auto* db = root->FirstChildElement("Database"))
        {
            const char* v = nullptr;
            if ((v = db->Attribute("host")))  cfg.dbHost = v;
            if ((v = db->Attribute("port")))  cfg.dbPort = atoi(v);
            if ((v = db->Attribute("user")))  cfg.dbUser = v;
            if ((v = db->Attribute("pass")))  cfg.dbPass = v;
            if ((v = db->Attribute("name")))  cfg.dbName = v;
        }
        // SuperServer
        if (auto* ss = root->FirstChildElement("SuperServer"))
        {
            const char* v = nullptr;
            if ((v = ss->Attribute("ip")))   cfg.superIP   = v;
            if ((v = ss->Attribute("port"))) cfg.superPort = atoi(v);
        }
        // 各服务器端口
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

        // 日志路径
        if (auto* lp = root->FirstChildElement("LogPaths"))
        {
            for (auto* e = lp->FirstChildElement(); e; e = e->NextSiblingElement())
                cfg.logPaths[e->Value()] = e->GetText() ? e->GetText() : "";
        }
        return true;
    }
};
