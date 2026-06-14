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
/** @brief 默认外联服客户端配置（游戏区连接 Logger/Global/Zone） */
constexpr const char* LOGIN_SERVER_LIST_DEFAULT = "loginserverlist.xml";
/** @brief 环境变量：覆盖 loginserverlist.xml 路径 */
constexpr const char* ENV_LOGIN_SERVER_LIST_PATH = "RPG_LOGIN_SERVER_LIST_PATH";
/** @brief 默认 LoggerServer 独立部署配置（各服目录，便于版本打包） */
constexpr const char* EXTERN_LOGGER_CONFIG_DEFAULT = "LoggerServer/extern_logger.xml";
/** @brief 环境变量：覆盖 extern_logger.xml 路径 */
constexpr const char* ENV_EXTERN_LOGGER_CONFIG = "RPG_EXTERN_LOGGER_CONFIG";
/** @brief 默认 GlobalServer 独立部署配置 */
constexpr const char* EXTERN_GLOBAL_CONFIG_DEFAULT = "GlobalServer/extern_global.xml";
constexpr const char* ENV_EXTERN_GLOBAL_CONFIG = "RPG_EXTERN_GLOBAL_CONFIG";
/** @brief 默认 ZoneServer 独立部署配置 */
constexpr const char* EXTERN_ZONE_CONFIG_DEFAULT = "ZoneServer/extern_zone.xml";
constexpr const char* ENV_EXTERN_ZONE_CONFIG = "RPG_EXTERN_ZONE_CONFIG";

constexpr const char* EXTERN_LOGIN_CONFIG_DEFAULT = "LoginServer/extern_login.xml";
constexpr const char* ENV_EXTERN_LOGIN_CONFIG = "RPG_EXTERN_LOGIN_CONFIG";
/** @brief 默认 LoginServer 游戏区列表配置路径 */
constexpr const char* SERVER_LIST_PATH_DEFAULT = "LoginServer/serverlist.xml";

/**
 * @brief 设置错误输出字符串
 * @param errOut 可选错误输出指针，可为空
 * @param msg 错误信息文本
 */
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

/**
 * @brief 解析十进制整数字符串
 * @param value 输入 C 字符串
 * @param fallback 解析失败或空串时返回值
 * @return 解析后的整数
 */
inline int parseInt(const char* value, int fallback)
{
    if (!value || value[0] == '\0')
        return fallback;
    return std::atoi(value);
}

/**
 * @brief 解析十进制无符号整数字符串
 * @param value 输入 C 字符串
 * @param fallback 解析失败或空串时返回值
 * @return 解析后的 uint32_t 值
 */
inline uint32_t parseUInt32(const char* value, uint32_t fallback)
{
    if (!value || value[0] == '\0')
        return fallback;
    return static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
}

/**
 * @brief 读取 XML 属性并解析为 int
 * @param elem XML 元素
 * @param name 属性名
 * @param fallback 属性缺失或非法时返回值
 * @return 属性整数值
 */
inline int readIntAttr(const tinyxml2::XMLElement* elem, const char* name, int fallback)
{
    if (!elem || !name)
        return fallback;
    return parseInt(elem->Attribute(name), fallback);
}

/**
 * @brief 读取 XML 属性并解析为 uint32_t
 * @param elem XML 元素
 * @param name 属性名
 * @param fallback 属性缺失或非法时返回值
 * @return 属性无符号整数值
 */
inline uint32_t readUIntAttr(const tinyxml2::XMLElement* elem, const char* name,
                             uint32_t fallback)
{
    if (!elem || !name)
        return fallback;
    return parseUInt32(elem->Attribute(name), fallback);
}

/**
 * @brief 读取 XML 字符串属性
 * @param elem XML 元素
 * @param name 属性名
 * @param out [out] 属性值（仅在属性存在时覆盖）
 */
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
