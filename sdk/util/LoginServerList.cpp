/**
 * @file    LoginServerList.cpp
 * @brief   loginserverlist.xml 解析实现
 */

#include "LoginServerList.h"
#include "XmlConfigUtil.h"

#include <tinyxml2.h>

#include <cstring>

namespace
{
/** @brief 解析 reconnect 属性（1/true/yes 为 true） */
bool parseReconnectAttr(const tinyxml2::XMLElement* elem, bool fallback)
{
    if (!elem)
        return fallback;
    const char* v = elem->Attribute("reconnect");
    if (!v || v[0] == '\0')
        return fallback;
    if (v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y')
        return true;
    if (v[0] == '0' || v[0] == 'f' || v[0] == 'F' || v[0] == 'n' || v[0] == 'N')
        return false;
    return fallback;
}

/** @brief 按 XML 标签名映射 SubServerType */
SubServerType tagToType(const char* tag)
{
    if (!tag)
        return SubServerType::UNKNOWN;
    if (std::strcmp(tag, "LoggerServer") == 0)
        return SubServerType::LOGGER;
    if (std::strcmp(tag, "GlobalServer") == 0)
        return SubServerType::GLOBAL;
    if (std::strcmp(tag, "ZoneServer") == 0)
        return SubServerType::ZONE;
    if (std::strcmp(tag, "LoginServer") == 0)
        return SubServerType::LOGIN;
    return SubServerType::UNKNOWN;
}

void loadNode(const tinyxml2::XMLElement* elem, LoginServerList& out)
{
    SubServerType type = tagToType(elem->Name());
    if (type == SubServerType::UNKNOWN)
        return;

    ExternalServerEntry e;
    e.type = type;
    XmlConfig::readStrAttr(elem, "ip", e.ip);
    e.port = (uint16_t)XmlConfig::readIntAttr(elem, "port", 0);
    e.reconnect = parseReconnectAttr(elem, false);
    if (e.port > 0)
        out.add(e);
}
}  // namespace

void LoginServerList::add(const ExternalServerEntry& entry)
{
    m_entries.push_back(entry);
}

void LoginServerList::clear()
{
    m_entries.clear();
}

size_t LoginServerList::size() const
{
    return m_entries.size();
}

const std::vector<ExternalServerEntry>& LoginServerList::all() const
{
    return m_entries;
}

const ExternalServerEntry* LoginServerList::find(SubServerType type) const
{
    for (const auto& e : m_entries)
    {
        if (e.type == type && e.port > 0)
            return &e;
    }
    return nullptr;
}

bool LoginServerListLoader::Load(const char* path, LoginServerList& out, std::string* errOut)
{
    out.clear();
    if (!path || path[0] == '\0')
    {
        XmlConfig::setError(errOut, "loginserverlist path is empty");
        return false;
    }

    tinyxml2::XMLDocument doc;
    const tinyxml2::XMLError code = doc.LoadFile(path);
    if (code == tinyxml2::XML_ERROR_FILE_NOT_FOUND)
        return true;  // 可选文件：不存在视为未配置外联
    if (code != tinyxml2::XML_SUCCESS)
    {
        XmlConfig::setError(errOut, std::string("failed to load loginserverlist: ") + path);
        return false;
    }

    tinyxml2::XMLElement* root = XmlConfig::requireRoot(doc, "LoginServerList", errOut);
    if (!root)
        return false;

    for (auto* e = root->FirstChildElement(); e; e = e->NextSiblingElement())
        loadNode(e, out);

    return true;
}
