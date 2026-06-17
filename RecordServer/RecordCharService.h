/**
 * @file    RecordCharService.h
 * @brief   RecordServer 角色列表/创角服务
 */

#pragma once

#include "../protocal/InternalMsg.h"
#include "../sdk/net/NetDefine.h"
#include <mysql/mysql.h>
#include <vector>

/**
 * @brief 角色登录辅助服务（Gateway 鉴权链路）
 */
class RecordCharService
{
public:
    /** @brief 查询账号下角色列表 */
    static void listCharacters(MYSQL* db, const Msg_REC_ListCharactersReq& req,
                               Msg_REC_ListCharactersRspHeader& hdr,
                               std::vector<Msg_REC_CharacterEntryWire>& entries);

    /** @brief 创建角色 */
    static void createCharacter(MYSQL* db, const Msg_REC_CreateCharacterReq& req,
                                Msg_REC_CreateCharacterRsp& rsp);
};
