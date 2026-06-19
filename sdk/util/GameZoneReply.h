/**
 * @file    GameZoneReply.h
 * @brief   外联服对 EXT_GAMEZONE_FWD_REQ 的对称回包（EXT_GAMEZONE_FWD_RSP）
 *
 * 区内服经 Super SS_EXTERN_FWD 访问 Login/Logger/Global/Zone 时，
 * 外联服 handler 须用本模块回包，Super 才能转 SS_EXTERN_FWD_RSP 到源区内服。
 *
 * 上下文按 (SuperConnID, seq) 索引；gameZoneSendForwardRsp 尝试回包后清除。
 * GameZoneOnForwardReq 在 handler 返回后若上下文仍在则强制清除（防止泄漏）。
 */

#pragma once

#include "../net/NetDefine.h"
#include "../../protocal/InternalMsg.h"

class TcpServer;

/**
 * @brief 入站 EXT_GAMEZONE_FWD 信封上下文
 */
struct GameZoneForwardContext
{
    Msg_SS_ExternForward reqHdr; /**< 入站请求信封（含 source/seq/innerMsgId） */
};

/**
 * @brief 保存待对称回包的入站信封（seq=0 时 no-op）
 * @param superConn Super 在外联 RegisterListen 上的连接 ID
 * @param reqHdr    入站 EXT_GAMEZONE_FWD 信封
 */
void gameZonePushForwardContext(ConnID superConn, const Msg_SS_ExternForward& reqHdr);

/**
 * @brief 清除 (superConn, seq) 上的转发上下文
 * @param superConn Super 连接 ID
 * @param seq       请求序号（与 reqHdr.seq 一致）
 */
void gameZonePopForwardContext(ConnID superConn, uint32_t seq);

/**
 * @brief 读取 (superConn, seq) 上的入站转发信封
 * @param superConn Super 连接 ID
 * @param seq       请求序号
 * @return 有效指针；无上下文或 seq=0 时 nullptr
 */
const GameZoneForwardContext* gameZonePeekForwardContext(ConnID superConn, uint32_t seq);

/**
 * @brief 读取 GameZoneOnForwardReq 同步 Dispatch 期间绑定的入站信封
 * @param superConn Super 连接 ID（须与 Dispatch 时 fromConn 一致）
 * @return 有效指针；非 Dispatch 栈内或 conn 不匹配时 nullptr
 */
const GameZoneForwardContext* gameZoneCurrentForwardContext(ConnID superConn);

/**
 * @brief 绑定/清除同步 Dispatch 期间的当前转发上下文（仅 GameZoneMsgDispatch 调用）
 */
void gameZoneSetCurrentForwardContext(ConnID superConn, const GameZoneForwardContext* ctx);

/**
 * @brief 向 Super 发送 EXT_GAMEZONE_FWD_RSP（对称回包）
 * @param registerServer Login 等外联服的 RegisterListen TcpServer
 * @param superConn      Super 连接 ID（与 reqHdr 对应）
 * @param reqHdr         入站请求信封（回包时交换 source/target，echo seq/innerMsgId）
 * @param code           信封层结果；0=成功且 innerBody 有效
 * @param innerBody      inner 业务 body；可为 nullptr（dataLen=0）
 * @param innerLen       inner body 字节数
 * @return 发送成功 true；seq!=0 时无论成败均清除上下文
 */
bool gameZoneSendForwardRsp(TcpServer& registerServer, ConnID superConn,
                            const Msg_SS_ExternForward& reqHdr, int32_t code,
                            const void* innerBody, uint16_t innerLen);
