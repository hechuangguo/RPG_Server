/**
 * @file    XmlConfigUtil.h
 * @brief   XML 配置解析公共工具（tinyxml2）
 *
 * 各 ConfigLoader 共用：文档加载、根节点校验、属性/文本读取与路径解析。
 */

#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <tinyxml2.h>

namespace XmlConfig {

/** @brief 默认全局配置路径（相对进程 cwd，RunServer.sh 在项目根启动） */
constexpr const char* CONFIG_PATH_DEFAULT = "config/config.xml";
/** @brief 默认 SceneServer 地图配置路径 */
constexpr const char* SCENE_INFO_PATH_DEFAULT = "config/server_info.xml";
/** @brief 环境变量：覆盖 config.xml 路径 */
constexpr const char* ENV_CONFIG_PATH = "RPG_CONFIG_PATH";
/** @brief 环境变量：覆盖 server_info.xml 路径 */
constexpr const char* ENV_SCENE_INFO_PATH = "RPG_SCENE_INFO_PATH";

inline void setError(std::string* errOut, const std::string& msg)
{
    if (errOut)
        *errOut = msg;
}

/**
 * @brief 解析配置文件路径：argv > 环境变量 > 默认值
 */
inline const char* resolvePath(int argc, char* argv[], int argIndex,
                              const char* envName, const char* defaultPath)
{
    if (argc > argIndex && argv[argIndex] && argv[argIndex][0] != '\0')
        return argv[argIndex];
    if (envName)
    {
        if (const char* env = std::getenv(envName))
        {
            if (env[0] != '\0')
                return env;
        }
    }
    return defaultPath;
}

/**
 * @brief 加载 XML 文件；失败时 errOut 含 tinyxml2 错误与行号
 */
inline bool loadDocument(const char* path, tinyxml2::XMLDocument& doc, std::string* errOut)
{
    if (!path || path[0] == '\0')
    {
        setError(errOut, "config path is empty");
        return false;
    }

    const tinyxml2::XMLError code = doc.LoadFile(path);
    if (code == tinyxml2::XML_SUCCESS)
        return true;

    std::string msg = "failed to load XML: ";
    msg += path;
    if (doc.ErrorStr() && doc.ErrorStr()[0] != '\0')
    {
        msg += " (";
        msg += doc.ErrorStr();
        if (doc.ErrorLineNum() > 0)
        {
            msg += ", line ";
            msg += std::to_string(doc.ErrorLineNum());
        }
        msg += ")";
    }
    setError(errOut, msg);
    return false;
}

/**
 * @brief 获取根元素并校验标签名
 */
inline tinyxml2::XMLElement* requireRoot(tinyxml2::XMLDocument& doc,
                                         const char* expectedTag,
                                         std::string* errOut)
{
    tinyxml2::XMLElement* root = doc.RootElement();
    if (!root)
    {
        setError(errOut, "XML has no root element");
        return nullptr;
    }
    if (expectedTag && std::strcmp(root->Name(), expectedTag) != 0)
    {
        setError(errOut,
                 std::string("unexpected root <") + root->Name() + ">, expected <"
                 + expectedTag + ">");
        return nullptr;
    }
    return root;
}

/** @brief 去除首尾空白 */
inline std::string trimCopy(const char* text)
{
    if (!text)
        return {};
    const char* begin = text;
    while (*begin && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    if (*begin == '\0')
        return {};
    const char* end = text + std::strlen(text);
    while (end > begin && std::isspace(static_cast<unsigned char>(end[-1])))
        --end;
    return std::string(begin, end);
}

inline int parseInt(const char* value, int fallback)
{
    if (!value || value[0] == '\0')
        return fallback;
    return std::atoi(value);
}

inline uint32_t parseUInt32(const char* value, uint32_t fallback)
{
    if (!value || value[0] == '\0')
        return fallback;
    return static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
}

inline int readIntAttr(const tinyxml2::XMLElement* elem, const char* name, int fallback)
{
    if (!elem || !name)
        return fallback;
    return parseInt(elem->Attribute(name), fallback);
}

inline uint32_t readUIntAttr(const tinyxml2::XMLElement* elem, const char* name,
                             uint32_t fallback)
{
    if (!elem || !name)
        return fallback;
    return parseUInt32(elem->Attribute(name), fallback);
}

inline void readStrAttr(const tinyxml2::XMLElement* elem, const char* name, std::string& out)
{
    if (!elem || !name)
        return;
    if (const char* v = elem->Attribute(name))
        out = v;
}

/** @brief 读取子元素文本（去首尾空白） */
inline std::string readElementText(const tinyxml2::XMLElement* elem)
{
    return trimCopy(elem ? elem->GetText() : nullptr);
}

} // namespace XmlConfig
