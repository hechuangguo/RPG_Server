/**
 * @file    SceneInfoLoader.h
 * @brief  场景服务器地图配置解析器 —— server_info.xml → SceneServerInfo
 *
 * 每个 SceneServer 进程对应一份 server_info.xml，描述该进程
 * 承载的地图列表及参数。
 *
 * XML 结构参考：
 * @code
 *   <SceneServerInfo sceneID="1">
 *     <Map id="1001" name="新手村" file="map/1001.map" maxPlayer="200"/>
 *     <Map id="1002" name="主城"   file="map/1002.map" maxPlayer="500"/>
 *   </SceneServerInfo>
 * @endcode
 */

#pragma once
#include <string>
#include <vector>
#include <tinyxml2.h>

/**
 * @brief 单个地图的配置信息
 */
struct MapConfig
{
    uint32_t    mapID    = 0;     /**< 地图唯一 ID */
    std::string mapName;          /**< 地图显示名 */
    std::string mapFile;          /**< 地图资源文件路径（如 map/1001.map） */
    uint32_t    maxPlayer= 200;   /**< 地图最大玩家数 */
};

/**
 * @brief 单个 SceneServer 进程的完整配置
 */
struct SceneServerInfo
{
    uint32_t             sceneID = 0;  /**< 场景服务器编号（唯一） */
    std::vector<MapConfig> maps;       /**< 该进程承载的地图列表 */
};

/**
 * @brief 场景信息加载器（静态工具类）
 */
class SceneInfoLoader
{
public:
    /**
     * @brief 从 XML 文件加载场景配置
     * @param path server_info.xml 文件路径
     * @param info [out] 填充的场景配置结构
     * @return 成功返回 true
     */
    static bool Load(const char* path, SceneServerInfo& info)
    {
        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path) != tinyxml2::XML_SUCCESS) return false;
        auto* root = doc.RootElement();
        if (!root) return false;

        // ── 场景服务器编号 ──
        if (const char* v = root->Attribute("sceneID"))
            info.sceneID = (uint32_t)atoi(v);

        // ── 遍历地图 ──
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
