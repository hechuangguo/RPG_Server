/**
 * @file    SuperExternRouter.h
 * @brief   SuperServer 外联转发路由（区内服 ↔ 独立服）
 *
 * 双向路由：
 *   - 区内服 → Super：SS_EXTERN_FWD_REQ（Msg_SS_ExternForward + inner body）
 *   - Super → 独立服：EXT_GAMEZONE_FWD_REQ（同信封，SuperExternSendToExtern）
 *   - 独立服 → Super：EXT_GAMEZONE_FWD_RSP（需应答业务）
 *   - Super → 区内服：SS_EXTERN_FWD_RSP（按 RSP.targetServerType 路由；该字段存原请求区内服类型）
 */

#pragma once

#include "../protocal/InternalMsg.h"
#include "../sdk/net/NetDefine.h"

class SuperServer;

/**
 * @brief 注册 Super 外联转发相关消息处理器
 * @param super SuperServer 实例（SS_EXTERN_FWD_REQ、EXT_GAMEZONE_FWD_RSP）
 */
void SuperExternMsgRegister(SuperServer& super);

/**
 * @brief 处理区内服 SS_EXTERN_FWD_REQ，转发到目标独立服
 * @param super    SuperServer 实例
 * @param fromConn 发起方区内服在 Super 侧的 connID
 * @param data     Msg_SS_ExternForward + inner body
 * @param len      总长度
 */
void SuperExternOnForwardReq(SuperServer& super, ConnID fromConn,
                             const char* data, uint16_t len);

/**
 * @brief 处理独立服 EXT_GAMEZONE_FWD_RSP，回包到源区内服
 * @param super          SuperServer 实例
 * @param fromExternConn 外联服连接 ID
 * @param data           Msg_SS_ExternForwardRsp + inner body
 * @param len            总长度
 */
void SuperExternOnForwardRsp(SuperServer& super, ConnID fromExternConn,
                             const char* data, uint16_t len);

/**
 * @brief 向独立服发送 EXT_GAMEZONE_FWD_REQ
 * @param super      SuperServer 实例
 * @param targetType LOGIN / LOGGER / GLOBAL / ZONE
 * @param hdr        已填好的转发头
 * @param body       inner body（可为 nullptr 若 dataLen=0）
 * @return 发送成功 true
 */
bool SuperExternSendToExtern(SuperServer& super, SubServerType targetType,
                             const Msg_SS_ExternForward& hdr, const char* body);

/**
 * @brief 向区内服发送 SS_EXTERN_FWD_RSP
 * @param super        SuperServer 实例
 * @param gameZoneConn 目标区内服 connID
 * @param hdr          响应头
 * @param body         inner body（可为 nullptr）
 * @return 发送成功 true
 */
bool SuperExternSendRspToGameZone(SuperServer& super, ConnID gameZoneConn,
                                  const Msg_SS_ExternForwardRsp& hdr,
                                  const char* body);
