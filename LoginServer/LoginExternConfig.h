/**
 * @file    LoginExternConfig.h
 * @brief   LoginServer 独立部署配置 —— extern_login.xml 解析
 *
 * 双端口：ClientListen（玩家登录）、RegisterListen（网关注册/心跳）。
 */

#pragma once

#include "../sdk/util/ExternServerConfig.h"
#include "../sdk/util/XmlConfigUtil.h"

#include <cstdint>
#include <string>

#include <tinyxml2.h>

/**
 * @brief LoginServer 进程配置
 */
struct LoginExternConfig
{
    std::string clientListenIP   = "0.0.0.0"; /**< 客户端登录监听 IP */
    uint16_t    clientListenPort = 0;          /**< 客户端登录端口（如 9010） */
    std::string registerListenIP = "0.0.0.0"; /**< 网关注册监听 IP */
    uint16_t    registerListenPort = 0;      /**< 网关注册端口（如 19010） */
    std::string logPath = "logs/login.log";  /**< 本进程日志路径 */
    std::string serverListPath = XmlConfig::SERVER_LIST_PATH_DEFAULT; /**< 游戏区列表 serverlist.xml */
    DatabaseConfig database;                 /**< 账号库 rpg_login（GameUser/ZoneInfo） */
};

/**
 * @brief extern_login.xml 加载器
 */
class LoginExternConfigLoader
{
public:
    /**
     * @brief 加载 LoginServer 外联配置
     * @param path   LoginServer/extern_login.xml
     * @param cfg    [out] 解析结果
     * @param errOut 可选错误信息
     * @return 成功返回 true
     */
    static bool Load(const char* path, LoginExternConfig& cfg, std::string* errOut = nullptr)
    {
        tinyxml2::XMLDocument doc;
        if (!XmlConfig::loadDocument(path, doc, errOut))
            return false;

        tinyxml2::XMLElement* root = XmlConfig::requireRoot(doc, "ExternServer", errOut);
        if (!root)
            return false;

        cfg = LoginExternConfig{};
        if (auto* cl = root->FirstChildElement("ClientListen"))
        {
            XmlConfig::readStrAttr(cl, "ip", cfg.clientListenIP);
            cfg.clientListenPort = (uint16_t)XmlConfig::readIntAttr(cl, "port", 0);
        }
        if (auto* rl = root->FirstChildElement("RegisterListen"))
        {
            XmlConfig::readStrAttr(rl, "ip", cfg.registerListenIP);
            cfg.registerListenPort = (uint16_t)XmlConfig::readIntAttr(rl, "port", 0);
        }
        if (auto* logPath = root->FirstChildElement("LogPath"))
        {
            const std::string text = XmlConfig::readElementText(logPath);
            if (!text.empty())
                cfg.logPath = text;
        }
        if (auto* serverList = root->FirstChildElement("ServerList"))
        {
            std::string path;
            XmlConfig::readStrAttr(serverList, "path", path);
            if (!path.empty())
                cfg.serverListPath = path;
        }
        loadDatabase(root->FirstChildElement("Database"), cfg.database);
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
};
