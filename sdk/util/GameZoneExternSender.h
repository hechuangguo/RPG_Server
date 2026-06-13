/**
 * @file    GameZoneExternSender.h
 * @brief  游戏区进程经 SuperServer 转发到独立外联服
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
    GameZoneExternSender(TcpClient& superClient, SubServerType selfType, uint32_t selfId);

    bool sendToLoginServer(uint16_t innerMsgId, const char* data, uint16_t len, uint32_t seq = 0);
    bool sendToLoggerServer(uint16_t innerMsgId, const char* data, uint16_t len);
    bool sendToGlobalServer(uint16_t innerMsgId, const char* data, uint16_t len, uint32_t seq = 0);
    bool sendToZoneServer(uint16_t innerMsgId, const char* data, uint16_t len, uint32_t seq = 0);

    /** @brief Init 后设置本进程 serverID（用于 SS_EXTERN_FWD 源标识） */
    void setSelfId(uint32_t selfId) { m_selfId = selfId; }

private:
    bool sendForward(SubServerType target, uint16_t innerMsgId,
                     const char* data, uint16_t len, uint32_t seq);

    TcpClient&     m_superClient;
    SubServerType  m_selfType;
    uint32_t       m_selfId;
};
