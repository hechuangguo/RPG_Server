/**
 * @file    LoggerServer.h
 * @brief  日志服务器 —— 接收各服务器日志写入请求，统一落盘
 *
 * ## 职责
 * - 接收来自各服务器的 LOG_WRITE_REQ 消息
 * - 按服务器类型双文件落盘：scene.log + scene.log.YYYYMMDD-HH
 * - 跨小时自动切换归档文件
 * - 支持按日志级别过滤，可选忽略低级别日志
 *
 * ## 日志文件命名规则
 * @code
 *   实时文件：scene.log（供 log.sh 实时查看，始终指向当前小时的最新日志）
 *   归档文件：scene.log.20260524-12（按小时归档，格式为 YYYYMMDD-HH）
 *
 *   双文件机制：
 *   - scene.log        → 符号链接或硬链接，始终指向当前小时的归档文件
 *   - scene.log.20260524-12 → 实际的数据文件
 *
 *   当时间从 HH:59:59 跨越到 HH+1:00:00 时，LogFileWriter 自动执行归档操作：
 *   1. 关闭当前小时的数据文件
 *   2. 创建新的归档文件（新小时时间戳）
 *   3. 更新实时文件指向新的归档文件
 *   后续写入自动进入新文件，无需人工干预。
 * @endcode
 *
 * ## 日志归档机制说明
 *
 * LogFileWriter 实现了自动化的日志轮转（Log Rotation）：
 *
 * ### 归档触发条件
 *
 * 归档由**时间驱动**，而非文件大小驱动。每当系统小时发生变化时触发归档。
 * LogFileWriter 在每次 Write() 时检查当前系统小时是否与文件创建时的小时一致，
 * 若不一致则自动执行归档操作。
 *
 * ### 归档流程
 *
 * 1. 获取当前系统时间，格式化为 "YYYYMMDD-HH" 字符串。
 * 2. 将当前的实时文件（如 scene.log）重命名为归档文件（如 scene.log.20260524-12）。
 * 3. 创建新的空数据文件，路径为 basePath + ".YYYYMMDD-HH"。
 * 4. 将实时文件（scene.log）指向新的数据文件。
 * 5. 后续所有 Write() 调用写入新文件。
 *
 * ### 归档文件清理
 *
 * 当前实现未包含自动清理旧归档文件的逻辑。建议通过外部脚本（如 cron 任务）或
 * 在 LogFileWriter 中增加 maxArchiveDays 配置，定期删除超过保留期限的归档文件，
 * 避免磁盘空间无限增长。
 *
 * ## 日志级别过滤说明
 *
 * Msg_Log_WriteReq 中包含 level 字段，标识该条日志的级别：
 *
 * | 级别值 | 枚举名 | 含义     | 说明                                       |
 * |--------|--------|----------|--------------------------------------------|
 * | 0      | DEBUG  | 调试信息 | 开发调试用，生产环境通常过滤掉              |
 * | 1      | INFO   | 一般信息 | 服务启动、关键操作等正常运行信息             |
 * | 2      | WARN   | 警告信息 | 可恢复的异常，如重试、降级等                |
 * | 3      | ERR    | 错误信息 | 需要关注的错误，如数据库写入失败             |
 * | 4      | FATAL  | 致命错误 | 服务无法继续运行，如端口绑定失败             |
 *
 * ### 过滤策略
 *
 * 当前 LoggerServer 接收所有级别的日志并直接落盘，不做过滤。
 * 生产环境中建议根据需求配置最低日志级别：
 *
 * - 通过配置文件或环境变量设置全局最低级别（如 LOG_MIN_LEVEL=1 表示只记录 INFO 及以上）。
 * - 在 OnWriteLog() 中增加级别检查：if (req->level < m_minLevel) return;
 * - 不同服务器类型可配置不同的最低级别，如 SceneServer 记录 WARN+，
 *   而 AOIServer 可记录 DEBUG+ 用于排查视野问题。
 *
 * ## 依赖关系
 * - 依赖 SuperServer + SessionServer
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/log/Logger.h"
#include "../sdk/log/LogFileWriter.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include <unordered_map>
#include <fstream>
#include <string>
#include <vector>

/**
 * @brief 远程日志写入请求结构
 *
 * 后跟 logLen 字节的日志文本（纯文本，不含换行）。
 *
 * ## 字段说明
 * - serverType: 标识来源服务器的类型（Session/Scene/AOI 等），
 *   LoggerServer 根据此字段选择对应的 LogFileWriter 实例。
 * - level: 日志级别（0-4），可用于级别过滤（见文件顶部详细说明）。
 * - logLen: 后续日志文本的字节长度，不含结构体头部。
 */
#pragma pack(push, 1)
struct Msg_Log_WriteReq
{
    uint8_t  serverType;  /**< 来源服务器类型（SubServerType 枚举值） */
    uint8_t  level;       /**< 日志级别：0=DEBUG 1=INFO 2=WARN 3=ERR 4=FATAL */
    uint32_t logLen;      /**< 日志文本长度（字节）—— 后跟 logLen 字节数据 */
};
#pragma pack(pop)

/**
 * @brief LoggerServer 核心类
 *
 * 单进程运行，维护按服务器类型索引的 LogFileWriter 句柄表。
 * 每种服务器类型对应一个独立的日志文件系列（实时文件 + 按小时归档文件）。
 */
class LoggerServer : public INetCallback
{
public:
    /** @brief 构造 LoggerServer（初始化写入器索引） */
    LoggerServer();

    /**
     * @brief 初始化 LoggerServer
     * @param ip         监听 IP
     * @param port       监听端口
     * @param superIP    SuperServer IP
     * @param superPort  SuperServer 端口
     * @param sessionIP  SessionServer IP
     * @param sessionPort SessionServer 端口
     * @param logDir     日志输出目录（绝对或相对路径）
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port,
              const std::string& superIP, uint16_t superPort,
              const std::string& sessionIP, uint16_t sessionPort,
              const std::string& logDir);

    /** @brief 主循环 */
    void Run();

    /** @brief 远程日志写入端连接建立 */
    void OnConnect(ConnID id) override;

    /** @brief 远程日志写入端断开 */
    void OnDisconnect(ConnID id) override;

    /** @brief 处理 LOG_WRITE_REQ 等日志协议 */
    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override;

private:
    /** @brief 注册日志写入相关消息处理器 */
    void RegisterHandlers();

    /**
     * @brief 处理远程日志写入请求
     *
     * 解析 Msg_Log_WriteReq 头部，提取日志文本，写入对应文件。
     * 文件按需打开（首次写入某服务器的日志时通过 GetWriter 创建）。
     *
     * 写入后追加换行符并立即 Flush，确保日志在异常情况下不会丢失。
     */
    void OnWriteLog(ConnID fromConn, const char* data, uint16_t len);

    /**
     * @brief 根据服务器类型返回日志文件基础名
     *
     * 映射关系：SubServerType 枚举值 → 对应的日志文件名（不含目录和归档后缀）。
     * 例如：SubServerType::SCENE → "scene.log"
     */
    static const char* ServerLogBaseName(SubServerType type);

    /**
     * @brief 获取或创建指定服务器类型的 LogFileWriter
     *
     * 首次访问某种服务器类型的日志时，创建 LogFileWriter 实例并存入 m_writers。
     * LogFileWriter 内部会自动管理按小时归档的文件切换。
     *
     * @param type 服务器类型
     * @return 对应的 LogFileWriter 引用
     */
    LogFileWriter& GetWriter(SubServerType type);

    /** @brief 向 SuperServer 注册 Logger 节点 */
    void RegisterToSuper();

    /** @brief 定时上报 Logger 存活心跳 */
    void SendHeartbeat();
    TcpServer  m_server;         /**< 内部连接监听 */
    TcpClient  m_superClient;    /**< 到 SuperServer 的连接 */
    TcpClient  m_sessionClient;  /**< 到 SessionServer 的连接 */
    uint32_t   m_hbSeq = 0;      /**< 心跳序列号 */
    std::string m_logDir;        /**< 日志输出根目录 */
    /**
     * @brief 各服务器类型 → 双文件写入器
     *
     * Key: SubServerType 枚举值（int）
     * Value: LogFileWriter 实例，负责管理该服务器类型的日志文件写入和按小时归档。
     * 采用惰性创建策略：首次收到某类型日志时才创建对应的 Writer。
     */
    std::unordered_map<int, LogFileWriter> m_writers;
};
