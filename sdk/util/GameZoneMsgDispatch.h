/**
 * @file    GameZoneMsgDispatch.h
 * @brief   独立服：解包 EXT_GAMEZONE_FWD_REQ 并分发 inner 业务消息
 *
 * 流程：
 *   1. SuperServer 将区内服 SS_EXTERN_FWD_REQ 解包为 EXT_GAMEZONE_FWD_REQ 发到独立服 Listen 口
 *   2. 本模块注册 EXT_GAMEZONE_FWD_REQ handler，解析 Msg_SS_ExternForward 头（Internal 管道 Unwrap 阶段）
 *   3. 按 hdr.innerMsgId 调用 MsgDispatcher::Dispatch(innerMsgId, body)
 *   4. handler 须 gameZoneSendForwardRsp 对称回包；未回包时 Dispatch 收尾强制 pop 防泄漏
 * 各独立服 *InternMsgRegister 聚合时调用 GameZoneMsgRegisterForwardDispatch()，再注册 inner 处理器。
 */

#pragma once

#include "../net/NetDefine.h"

/**
 * @brief 注册 EXT_GAMEZONE_FWD_REQ 处理器（独立服 Listen 口共用）
 */
void GameZoneMsgRegisterForwardDispatch();

/**
 * @brief 解包 EXT_GAMEZONE_FWD_REQ 并按 innerMsgId 分发
 * @param fromConn Super 侧连接 ID（传给 inner handler）
 * @param data     Msg_SS_ExternForward + inner body
 * @param len      总长度
 */
void GameZoneOnForwardReq(ConnID fromConn, const char* data, uint16_t len);

/** @brief 对称回包见 GameZoneReply.h（gameZoneSendForwardRsp / gameZonePeekForwardContext） */
