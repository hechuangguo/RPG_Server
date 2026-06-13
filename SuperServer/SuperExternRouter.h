/**
 * @file    SuperExternRouter.h
 * @brief  SuperServer 外联转发路由（区内服 ↔ 独立服）
 */

#pragma once

#include "../protocal/InternalMsg.h"
#include "../sdk/net/NetDefine.h"

class SuperServer;

/** @brief 注册 Super 外联转发相关消息处理器 */
void SuperExternMsgRegister(SuperServer& super);

/** @brief 处理区内服 SS_EXTERN_FWD_REQ */
void SuperExternOnForwardReq(SuperServer& super, ConnID fromConn,
                             const char* data, uint16_t len);

/** @brief 处理独立服 EXT_GAMEZONE_FWD_RSP */
void SuperExternOnForwardRsp(SuperServer& super, ConnID fromExternConn,
                             const char* data, uint16_t len);

/** @brief 向独立服发送 EXT_GAMEZONE_FWD_REQ */
bool SuperExternSendToExtern(SuperServer& super, SubServerType targetType,
                             const Msg_SS_ExternForward& hdr, const char* body);

/** @brief 向区内服发送 SS_EXTERN_FWD_RSP */
bool SuperExternSendRspToGameZone(SuperServer& super, ConnID gameZoneConn,
                                  const Msg_SS_ExternForwardRsp& hdr,
                                  const char* body);
