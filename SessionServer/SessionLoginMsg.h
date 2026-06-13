/**
 * @file    SessionLoginMsg.h
 * @brief  SessionServer 处理经 Super 转发的 Login 区服指令
 */

#pragma once

class SessionServer;

void SessionLoginMsgRegister(SessionServer& server);
