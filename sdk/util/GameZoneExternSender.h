/**
 * @file    GameZoneExternSender.h
 * @brief   游戏区进程经 SuperServer 转发到独立外联服
 *
 * 职责：
 *   - 区内服（Session/Scene/Record/AOI 等）封装 SS_EXTERN_FWD_REQ 信封
 *   - 经出站 Super TcpClient 发往 SuperServer，由 SuperExternRouter 解包转发
 * 目标：LoginServer / LoggerServer / GlobalServer / ZoneServer
 */

#pragma once

#include "../net/TcpClient.h"
#include "../../protocal/InternalMsg.h"

/**
 * @brief 封装 SS_EXTERN_FWD_REQ，经 Super 转发到 Login/Logger/Global/Zone
 */
class GameZoneExternSender
{
public:
    /**
     * @brief 构造发送器
     * @param superClient 本进程到 SuperServer 的出站连接
     * @param selfType    本进程 SubServerType（写入信封 sourceServerType）
     * @param selfId      本进程 serverID（Init 前可为 0，之后可 setSelfId）
     */
    GameZoneExternSender(TcpClient& superClient, SubServerType selfType, uint32_t selfId);

    /**
     * @brief 转发到 LoginServer
     * @param innerMsgId 业务协议号（如 LOGIN_RECHARGE_REQ）
     * @param data       inner body
     * @param len        body 长度
     * @param seq        请求序号（需 RSP 时非 0）
     * @return 发送成功 true
     */
    bool sendToLoginServer(uint16_t innerMsgId, const char* data, uint16_t len, uint32_t seq = 0);

    /**
     * @brief 转发到 LoggerServer（如 LOG_WRITE_REQ，无 seq）
     * @param innerMsgId 业务协议号
     * @param data       inner body
     * @param len        body 长度
     * @return 发送成功 true
     */
    bool sendToLoggerServer(uint16_t innerMsgId, const char* data, uint16_t len);

    /**
     * @brief 转发到 GlobalServer
     * @param innerMsgId 业务协议号（如 GLB_RANK_UPDATE）
     * @param data       inner body
     * @param len        body 长度
     * @param seq        请求序号（需 RSP 时非 0）
     * @return 发送成功 true
     */
    bool sendToGlobalServer(uint16_t innerMsgId, const char* data, uint16_t len, uint32_t seq = 0);

    /**
     * @brief 转发到 ZoneServer
     * @param innerMsgId 业务协议号（如 ZONE_CROSS_REQ）
     * @param data       inner body
     * @param len        body 长度
     * @param seq        请求序号（需 RSP 时非 0）
     * @return 发送成功 true
     */
    bool sendToZoneServer(uint16_t innerMsgId, const char* data, uint16_t len, uint32_t seq = 0);

    /** @brief Init 后设置本进程 serverID（用于 SS_EXTERN_FWD 源标识） */
    void setSelfId(uint32_t selfId) { m_selfId = selfId; }

private:
    /**
     * @brief 构造 Msg_SS_ExternForward 并发送 SS_EXTERN_FWD_REQ
     * @param target     目标外联服类型
     * @param innerMsgId 业务协议号
     * @param data       inner body
     * @param len        body 长度
     * @param seq        配对 RSP 的序号（单向可 0）
     * @return 发送成功 true
     */
    bool sendForward(SubServerType target, uint16_t innerMsgId,
                     const char* data, uint16_t len, uint32_t seq);

    TcpClient&     m_superClient;  /**< 到 SuperServer 的出站连接 */
    SubServerType  m_selfType;     /**< 本进程类型（信封 sourceServerType） */
    uint32_t       m_selfId;       /**< 本进程 serverID（信封 sourceServerId） */
};
