/**
 * @file    RecordClient.cpp
 * @brief   RecordClient 实现
 */

#include "RecordClient.h"
#include "../sdk/log/Logger.h"

RecordClient::RecordClient()
    : ScenePeerClient("RecordClient")
{
}

void RecordClient::saveUser(const SceneUser& user)
{
    Msg_REC_SaveUserReq req{};
    req.userID = user.GetID();
    req.wire = toUserBaseWire(user.Base());
    sendMsg(static_cast<uint16_t>(InternalMsgID::REC_SAVE_USER_REQ),
            reinterpret_cast<char*>(&req), sizeof(req));
}

void RecordClient::onSaveRsp(const char* data, uint16_t len)
{
    if (len < sizeof(Msg_REC_LoadUserRsp))
        return;
    const auto* rsp = reinterpret_cast<const Msg_REC_LoadUserRsp*>(data);
    if (rsp->code != 0)
        LOG_WARN("存档客户端保存失败 userID=%llu code=%d", rsp->userID, rsp->code);
}
