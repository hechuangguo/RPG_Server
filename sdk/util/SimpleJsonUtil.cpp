/**
 * @file    SimpleJsonUtil.cpp
 * @brief  Common/map meta.json / spawns.json 轻量解析实现
 */

#include "SimpleJsonUtil.h"

#include "../log/Logger.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace
{

constexpr float DEFAULT_MAX_X = 512.f;
constexpr float DEFAULT_MAX_Y = 64.f;
constexpr float DEFAULT_MAX_Z = 512.f;

std::string readFileToString(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs)
        return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

bool findNumberAfterKey(const std::string& json, const std::string& key, double& out)
{
    const std::string needle = "\"" + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return false;
    size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos)
        return false;
    size_t i = colon + 1;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n'))
        ++i;
    char* end = nullptr;
    out = std::strtod(json.c_str() + static_cast<ptrdiff_t>(i), &end);
    return end != json.c_str() + static_cast<ptrdiff_t>(i);
}

bool findStringAfterKey(const std::string& json, const std::string& key, std::string& out)
{
    const std::string needle = "\"" + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return false;
    size_t q1 = json.find('"', json.find(':', pos) + 1);
    if (q1 == std::string::npos)
        return false;
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos)
        return false;
    out = json.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

bool parseBoundsObject(const std::string& json, MapWorldBounds& bounds)
{
    double v = 0;
    if (findNumberAfterKey(json, "minX", v)) bounds.minX = static_cast<float>(v);
    if (findNumberAfterKey(json, "minY", v)) bounds.minY = static_cast<float>(v);
    if (findNumberAfterKey(json, "minZ", v)) bounds.minZ = static_cast<float>(v);
    if (findNumberAfterKey(json, "maxX", v)) bounds.maxX = static_cast<float>(v);
    if (findNumberAfterKey(json, "maxY", v)) bounds.maxY = static_cast<float>(v);
    if (findNumberAfterKey(json, "maxZ", v)) bounds.maxZ = static_cast<float>(v);
    return bounds.maxX > bounds.minX && bounds.maxZ > bounds.minZ;
}

bool deriveBoundsFromSize(const std::string& json, MapWorldBounds& bounds)
{
    double width = 0;
    double height = 0;
    double tileW = 0;
    double tileH = 0;

    const size_t sizePos = json.find("\"size\"");
    if (sizePos != std::string::npos)
    {
        const size_t brace = json.find('{', sizePos);
        const size_t end = json.find('}', brace);
        if (brace != std::string::npos && end != std::string::npos)
        {
            const std::string block = json.substr(brace, end - brace + 1);
            findNumberAfterKey(block, "width", width);
            findNumberAfterKey(block, "height", height);
        }
    }

    const size_t tilePos = json.find("\"tileSize\"");
    if (tilePos != std::string::npos)
    {
        const size_t brace = json.find('{', tilePos);
        const size_t end = json.find('}', brace);
        if (brace != std::string::npos && end != std::string::npos)
        {
            const std::string block = json.substr(brace, end - brace + 1);
            findNumberAfterKey(block, "width", tileW);
            findNumberAfterKey(block, "height", tileH);
        }
    }

    if (width > 0 && height > 0 && tileW > 0 && tileH > 0)
    {
        bounds.minX = 0.f;
        bounds.minY = 0.f;
        bounds.minZ = 0.f;
        bounds.maxX = static_cast<float>(width * tileW);
        bounds.maxY = DEFAULT_MAX_Y;
        bounds.maxZ = static_cast<float>(height * tileH);
        return true;
    }

    bounds.minX = 0.f;
    bounds.minY = 0.f;
    bounds.minZ = 0.f;
    bounds.maxX = DEFAULT_MAX_X;
    bounds.maxY = DEFAULT_MAX_Y;
    bounds.maxZ = DEFAULT_MAX_Z;
    return true;
}

bool parseDefaultSpawn(const std::string& json, MapSpawnPoint& spawn)
{
    const size_t keyPos = json.find("\"defaultSpawn\"");
    if (keyPos == std::string::npos)
        return false;
    const size_t brace = json.find('{', keyPos);
    if (brace == std::string::npos)
        return false;
    const size_t end = json.find('}', brace);
    if (end == std::string::npos)
        return false;
    const std::string block = json.substr(brace, end - brace + 1);
    double v = 0;
    if (findNumberAfterKey(block, "x", v)) spawn.x = static_cast<float>(v);
    if (findNumberAfterKey(block, "y", v)) spawn.y = static_cast<float>(v);
    if (findNumberAfterKey(block, "z", v)) spawn.z = static_cast<float>(v);
    return true;
}

void parseSpawnObjects(const std::string& json, std::vector<MapSpawnPoint>& spawns)
{
    spawns.clear();
    size_t pos = 0;
    while (true)
    {
        pos = json.find('{', pos);
        if (pos == std::string::npos)
            break;
        size_t end = json.find('}', pos);
        if (end == std::string::npos)
            break;
        const std::string obj = json.substr(pos, end - pos + 1);
        if (obj.find("\"x\"") != std::string::npos)
        {
            MapSpawnPoint pt;
            findStringAfterKey(obj, "name", pt.name);
            findStringAfterKey(obj, "id", pt.name);
            double v = 0;
            if (findNumberAfterKey(obj, "x", v)) pt.x = static_cast<float>(v);
            if (findNumberAfterKey(obj, "y", v)) pt.y = static_cast<float>(v);
            if (findNumberAfterKey(obj, "z", v)) pt.z = static_cast<float>(v);
            spawns.push_back(pt);
        }
        pos = end + 1;
    }
}

} // namespace

bool loadMapRuntimeData(const std::string& mapDir, uint32_t mapId, MapRuntimeData& out,
                        std::string* errOut)
{
    auto fail = [&](const std::string& msg) {
        if (errOut)
            *errOut = msg;
        return false;
    };

    const std::string metaPath = mapDir + "/meta.json";
    const std::string metaJson = readFileToString(metaPath);
    if (metaJson.empty())
        return fail("无法读取 meta.json: " + metaPath);

    out = MapRuntimeData{};
    out.mapId = mapId;
    out.runtimeRoot = mapDir;

    double v = 0;
    if (findNumberAfterKey(metaJson, "mapId", v))
        out.mapId = static_cast<uint32_t>(v);
    if (findNumberAfterKey(metaJson, "version", v))
        out.version = static_cast<uint32_t>(v);
    findStringAfterKey(metaJson, "coordSystem", out.coordSystem);
    if (findNumberAfterKey(metaJson, "aoiGridSize", v))
        out.aoiGridSize = static_cast<float>(v);
    if (findNumberAfterKey(metaJson, "maxWalkSpeed", v))
        out.maxWalkSpeed = static_cast<float>(v);
    if (findNumberAfterKey(metaJson, "maxRunSpeed", v))
        out.maxRunSpeed = static_cast<float>(v);
    if (findNumberAfterKey(metaJson, "maxStepWalk", v))
        out.maxStepWalk = static_cast<float>(v);
    if (findNumberAfterKey(metaJson, "maxStepRun", v))
        out.maxStepRun = static_cast<float>(v);

    if (!parseBoundsObject(metaJson, out.bounds))
    {
        if (!deriveBoundsFromSize(metaJson, out.bounds))
            return fail("meta.json 边界无效");
    }

    parseDefaultSpawn(metaJson, out.defaultSpawn);

    const std::string spawnsPath = mapDir + "/spawns.json";
    const std::string spawnsJson = readFileToString(spawnsPath);
    if (!spawnsJson.empty())
        parseSpawnObjects(spawnsJson, out.spawns);

    const std::string navPath = mapDir + "/navmesh.bin";
    std::ifstream nav(navPath, std::ios::binary);
    out.hasNavMesh = nav.good();

    if (out.mapId != mapId)
        LOG_WARN("meta.json mapId=%u 与配置 %u 不一致", out.mapId, mapId);

    return true;
}
