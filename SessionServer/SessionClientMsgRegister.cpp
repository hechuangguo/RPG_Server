/**
 * @file    SessionClientMsgRegister.cpp
 * @brief  会话服客户端消息注册实现
 */

#include "SessionClientMsgRegister.h"
#include "SessionServer.h"

void SessionClientMsgRegister(SessionServer& /*server*/)
{
    // 会话域客户端消息（SOCIAL/QUEST 等）尚未实现；Gateway Router 当前不转发 SESSION。
    // 落地时在此 registerClient*，并同步 ClientMsgValidator + ClientMsgRouter。
}
