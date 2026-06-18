/**
 * @file    LoggerInternMsgRegister.cpp
 * @brief  日志服区内 S2S 消息注册实现
 */

#include "LoggerInternMsgRegister.h"
#include "LoggerGameZoneLogMsg.h"
#include "LoggerServer.h"
#include "../sdk/util/MsgHandlerBinder.h"
#include "../protocal/InternalMsg.h"

void LoggerInternMsgRegister(LoggerServer& server)
{
    auto& d = MsgDispatcher::Instance();
    registerInternalSized<LoggerServer, Msg_Log_WriteReq>(
        d, &server, static_cast<uint16_t>(InternalMsgID::LOG_WRITE_REQ),
        &LoggerServer::onWriteLog);
    LoggerGameZoneMsgRegister(server);
}
