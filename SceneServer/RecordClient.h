/**
 * @file    RecordClient.h
 * @brief   SceneServer → RecordServer 出站客户端
 *
 * 用户离线与存档时发送 REC_SAVE_USER_REQ。
 */

#pragma once

#include "ScenePeerClient.h"
#include "../protocal/InternalMsg.h"
#include "../sdk/util/UserWireUtil.h"
#include "SceneUser.h"

/**
 * @brief SceneServer 到 RecordServer 的出站连接
 */
class RecordClient : public ScenePeerClient
{
public:
    RecordClient();

    /** @brief 将 SceneUser 数据写入 RecordServer */
    void saveUser(const SceneUser& user);

    /** @brief 处理 REC_SAVE_USER_RSP（当前仅日志） */
    void onSaveRsp(const char* data, uint16_t len);
};
