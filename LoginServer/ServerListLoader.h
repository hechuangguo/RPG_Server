/**
 * @file    ServerListLoader.h
 * @brief   LoginServer 游戏区列表配置解析 —— serverlist.xml → ZoneInfoRow
 *
 * XML 结构参考：
 * @code
 *   <ServerList>
 *     <Zone zoneId="1" gameType="0" name="RPG一区"
 *           ip="127.0.0.1" superPort="9000" enabled="1"/>
 *   </ServerList>
 * @endcode
 */

#pragma once

#include "ZoneInfoStore.h"
#include "../sdk/util/XmlConfigUtil.h"

#include <string>
#include <unordered_set>
#include <vector>

#include <tinyxml2.h>

/**
 * @brief serverlist.xml 加载器（静态工具类）
 */
class ServerListLoader
{
public:
    /**
     * @brief 从 XML 文件加载游戏区列表
     * @param path   serverlist.xml 路径
     * @param rows   [out] 解析后的区服行
     * @param errOut 可选；失败时写入可读错误信息
     * @return 成功返回 true
     */
    static bool Load(const char* path, std::vector<ZoneInfoRow>& rows, std::string* errOut = nullptr)
    {
        tinyxml2::XMLDocument doc;
        if (!XmlConfig::loadDocument(path, doc, errOut))
            return false;

        tinyxml2::XMLElement* root = XmlConfig::requireRoot(doc, "ServerList", errOut);
        if (!root)
            return false;

        rows.clear();
        std::unordered_set<uint64_t> seenKeys;

        for (auto* e = root->FirstChildElement("Zone"); e; e = e->NextSiblingElement("Zone"))
        {
            ZoneInfoRow row;
            row.zoneId = XmlConfig::readUIntAttr(e, "zoneId", 0);
            if (row.zoneId == 0)
            {
                XmlConfig::setError(errOut, "Zone: zoneId must be > 0");
                return false;
            }
            row.gameType = static_cast<uint8_t>(XmlConfig::readUIntAttr(e, "gameType", 0));
            XmlConfig::readStrAttr(e, "name", row.name);
            XmlConfig::readStrAttr(e, "ip", row.ip);
            row.superPort = static_cast<uint16_t>(XmlConfig::readUIntAttr(e, "superPort", 0));
            row.enabled = XmlConfig::readUIntAttr(e, "enabled", 1) != 0;

            if (row.name.empty())
            {
                XmlConfig::setError(errOut,
                                     "Zone zoneId=" + std::to_string(row.zoneId) + ": name is required");
                return false;
            }
            if (row.ip.empty())
            {
                XmlConfig::setError(errOut,
                                     "Zone zoneId=" + std::to_string(row.zoneId) + ": ip is required");
                return false;
            }
            if (row.superPort == 0)
            {
                XmlConfig::setError(errOut,
                                     "Zone zoneId=" + std::to_string(row.zoneId) + ": superPort must be > 0");
                return false;
            }

            const uint64_t key = (static_cast<uint64_t>(row.gameType) << 32) | row.zoneId;
            if (!seenKeys.insert(key).second)
            {
                XmlConfig::setError(errOut,
                                     "Zone duplicate gameType=" + std::to_string(row.gameType)
                                         + " zoneId=" + std::to_string(row.zoneId));
                return false;
            }
            rows.push_back(std::move(row));
        }

        if (rows.empty())
        {
            XmlConfig::setError(errOut, "ServerList: at least one Zone entry is required");
            return false;
        }
        return true;
    }
};
