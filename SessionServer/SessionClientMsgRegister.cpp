/**
 * @file    SessionClientMsgRegister.cpp
 * @brief  会话服客户端消息注册实现
 */

#include "SessionClientMsgRegister.h"
#include "SessionServer.h"

void SessionClientMsgRegister(SessionServer& /*server*/)
{
    // 会话域客户端消息尚未实现；未注册消息由 MsgIngress::dispatchClient 打 WARN
}
