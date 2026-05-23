#pragma once
#include <string>
#include <vector>
#include <tinyxml2.h>

// ============================================================
//  场景服务器地图配置解析（server_info.xml）
// ============================================================
struct MapConfig
{
    uint32_t    mapID    = 0;
    std::string mapName;
    std::string mapFile;   // 地图资源文件路径
    uint32_t    maxPlayer= 200;
};

struct SceneServerInfo
{
    uint32_t             sceneID = 0;   // 场景服务器编号
    std::vector<MapConfig> maps;
};

class SceneInfoLoader
{
public:
    static bool Load(const char* path, SceneServerInfo& info)
    {
        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path) != tinyxml2::XML_SUCCESS) return false;
        auto* root = doc.RootElement();
        if (!root) return false;
        if (const char* v = root->Attribute("sceneID")) info.sceneID = (uint32_t)atoi(v);
        for (auto* e = root->FirstChildElement("Map"); e; e = e->NextSiblingElement("Map"))
        {
            MapConfig mc;
            if (const char* v = e->Attribute("id"))       mc.mapID    = (uint32_t)atoi(v);
            if (const char* v = e->Attribute("name"))     mc.mapName  = v;
            if (const char* v = e->Attribute("file"))     mc.mapFile  = v;
            if (const char* v = e->Attribute("maxPlayer"))mc.maxPlayer= (uint32_t)atoi(v);
            info.maps.push_back(mc);
        }
        return true;
    }
};
