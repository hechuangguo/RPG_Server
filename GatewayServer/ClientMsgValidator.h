/**
 * @file    ClientMsgValidator.h
 * @brief  Gateway 客户端消息安全校验（白名单、长度、状态、userID）
 */

#pragma once
#include "../Common/ClientTypes.h"
#include "../Common/ClientMsgBody.h"
#include "../Common/LoginMsg.h"
#include "../Common/MapDataMsg.h"
#include "../sdk/util/LoginSpawnConfig.h"
#include "../sdk/util/RoleNameUtil.h"
#include "../Common/ChatMsg.h"
#include "GatewayUser.h"
#include <cstdint>
#include <cstring>

/** @brief 校验结果 */
enum class ValidateResult : uint8_t
{
    OK = 0,       /**< 校验通过 */
    UNKNOWN_MSG,  /**< 未在白名单中的 module/sub */
    BAD_LENGTH,   /**< 包长不在允许范围 */
    BAD_STATE,    /**< 当前连接状态不允许该消息 */
    BAD_PAYLOAD,  /**< 包体语义非法（如 userID/坐标异常） */
    RATE_LIMITED, /**< 触发频率限制（预留） */
};

/**
 * @brief 客户端消息校验器（查表 + 状态机）
 */
class ClientMsgValidator
{
public:
    static ValidateResult check(const GatewayUser* user,
                                uint8_t module, uint8_t sub,
                                const char* data, uint16_t len)
    {
        if (!user)
            return ValidateResult::BAD_PAYLOAD;
        const MsgRule* rule = findRule(module, sub);
        if (!rule)
            return ValidateResult::UNKNOWN_MSG;
        if (!isStateAllowed(user->getClientState(), rule->allowedStates))
            return ValidateResult::BAD_STATE;
        if (len < rule->minLen || len > rule->maxLen)
            return ValidateResult::BAD_LENGTH;
        if (!clientMsgBodyMatches(module, sub, data, len))
            return ValidateResult::BAD_PAYLOAD;
        if (rule->needsInWorld && user->getClientState() != ClientState::IN_WORLD)
            return ValidateResult::BAD_STATE;
        if (rule->checkUserId &&
            len >= static_cast<uint16_t>(sizeof(ClientMsgBodyHead) + sizeof(uint64_t)))
        {
            uint64_t packetUserId = 0;
            memcpy(&packetUserId, data + sizeof(ClientMsgBodyHead), sizeof(uint64_t));
            if (user->GetID() != 0 && packetUserId != user->GetID())
                return ValidateResult::BAD_PAYLOAD;
        }
        if (module == Msg_C2S_MoveReq::kModule &&
            sub == static_cast<uint8_t>(SceneMsgSub::C2S_MOVE_REQ))
            return validateMove(data, len);
        if (module == Msg_C2S_Chat::kModule &&
            sub == static_cast<uint8_t>(ChatMsgSub::C2S_CHAT_REQ))
            return validateChat(data, len);
        if (module == Msg_C2S_GatewayAuthReq::kModule &&
            sub == static_cast<uint8_t>(LoginMsgSub::C2S_GATEWAY_AUTH_REQ))
            return validateGatewayAuth(data, len);
        if (module == Msg_C2S_CreateUserReq::kModule &&
            sub == static_cast<uint8_t>(LoginMsgSub::C2S_CREATE_USER_REQ))
            return validateCreateUser(data, len);
        if (module == Msg_C2S_LogoutReq::kModule &&
            sub == static_cast<uint8_t>(LoginMsgSub::C2S_LOGOUT_REQ))
            return validateLogout(data, len);
        return ValidateResult::OK;
    }

    static int32_t toErrorCode(ValidateResult result)
    {
        switch (result)
        {
        case ValidateResult::OK:           return static_cast<int32_t>(GatewayValidateCode::OK);
        case ValidateResult::UNKNOWN_MSG:  return static_cast<int32_t>(GatewayValidateCode::UNKNOWN_MSG);
        case ValidateResult::BAD_LENGTH:   return static_cast<int32_t>(GatewayValidateCode::BAD_LENGTH);
        case ValidateResult::BAD_STATE:    return static_cast<int32_t>(GatewayValidateCode::BAD_STATE);
        case ValidateResult::BAD_PAYLOAD:  return static_cast<int32_t>(GatewayValidateCode::BAD_PAYLOAD);
        case ValidateResult::RATE_LIMITED: return static_cast<int32_t>(GatewayValidateCode::RATE_LIMITED);
        }
        return static_cast<int32_t>(GatewayValidateCode::BAD_PAYLOAD);
    }

private:
    static constexpr uint8_t STATE_CONNECTED  = 1u << 0;
    static constexpr uint8_t STATE_AUTHING    = 1u << 1;
    static constexpr uint8_t STATE_ACCOUNT_OK = 1u << 2;
    static constexpr uint8_t STATE_ENTERING   = 1u << 3;
    static constexpr uint8_t STATE_IN_WORLD   = 1u << 4;

    struct MsgRule
    {
        uint8_t  module;
        uint8_t  sub;
        uint16_t minLen;
        uint16_t maxLen;
        uint8_t  allowedStates;
        bool     needsInWorld;
        bool     checkUserId;
    };

    static bool isStateAllowed(ClientState state, uint8_t mask)
    {
        uint8_t bit = 0;
        switch (state)
        {
        case ClientState::CONNECTED:  bit = STATE_CONNECTED; break;
        case ClientState::AUTHING:    bit = STATE_AUTHING; break;
        case ClientState::ACCOUNT_OK: bit = STATE_ACCOUNT_OK; break;
        case ClientState::ENTERING:   bit = STATE_ENTERING; break;
        case ClientState::IN_WORLD:   bit = STATE_IN_WORLD; break;
        }
        return (mask & bit) != 0;
    }

    static const MsgRule* findRule(uint8_t module, uint8_t sub)
    {
        static const MsgRule kRules[] = {
            {Msg_C2S_GatewayAuthReq::kModule, Msg_C2S_GatewayAuthReq::kSub,
             sizeof(Msg_C2S_GatewayAuthReq), sizeof(Msg_C2S_GatewayAuthReq),
             STATE_CONNECTED, false, false},
            {Msg_C2S_SelectUserReq::kModule, Msg_C2S_SelectUserReq::kSub,
             sizeof(Msg_C2S_SelectUserReq), sizeof(Msg_C2S_SelectUserReq),
             STATE_ACCOUNT_OK, false, false},
            {Msg_C2S_CreateUserReq::kModule, Msg_C2S_CreateUserReq::kSub,
             sizeof(Msg_C2S_CreateUserReq), sizeof(Msg_C2S_CreateUserReq),
             STATE_ACCOUNT_OK, false, false},
            {Msg_C2S_LogoutReq::kModule, Msg_C2S_LogoutReq::kSub,
             sizeof(Msg_C2S_LogoutReq), sizeof(Msg_C2S_LogoutReq),
             STATE_ENTERING | STATE_IN_WORLD, false, false},
            {Msg_C2S_Heartbeat::kModule, Msg_C2S_Heartbeat::kSub,
             sizeof(Msg_C2S_Heartbeat), sizeof(Msg_C2S_Heartbeat),
             STATE_CONNECTED | STATE_AUTHING | STATE_ACCOUNT_OK | STATE_ENTERING | STATE_IN_WORLD,
             false, false},
            {Msg_C2S_MoveReq::kModule, Msg_C2S_MoveReq::kSub,
             sizeof(Msg_C2S_MoveReq), sizeof(Msg_C2S_MoveReq),
             STATE_IN_WORLD, true, true},
            {Msg_C2S_Chat::kModule, Msg_C2S_Chat::kSub,
             sizeof(Msg_C2S_Chat), sizeof(Msg_C2S_Chat),
             STATE_IN_WORLD, true, false},
            {Msg_C2S_NpcTalkReq::kModule, Msg_C2S_NpcTalkReq::kSub,
             sizeof(Msg_C2S_NpcTalkReq), sizeof(Msg_C2S_NpcTalkReq),
             STATE_IN_WORLD, true, true},
        };
        for (const auto& rule : kRules)
        {
            if (rule.module == module && rule.sub == sub)
                return &rule;
        }
        return nullptr;
    }

    static ValidateResult validateGatewayAuth(const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_GatewayAuthReq))
            return ValidateResult::BAD_LENGTH;
        const auto* req = reinterpret_cast<const Msg_C2S_GatewayAuthReq*>(data);
        if (req->account[0] == '\0' || req->loginToken[0] == '\0')
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }

    static ValidateResult validateCreateUser(const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_CreateUserReq))
            return ValidateResult::BAD_LENGTH;
        const auto* req = reinterpret_cast<const Msg_C2S_CreateUserReq*>(data);
        if (req->name[0] == '\0')
            return ValidateResult::BAD_PAYLOAD;
        if (!isValidRoleNameUtf8(req->name))
            return ValidateResult::BAD_PAYLOAD;
        if (req->vocation > MAX_VOCATION_ID || req->sex > MAX_SEX_ID)
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }

    static ValidateResult validateLogout(const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_LogoutReq))
            return ValidateResult::BAD_LENGTH;
        const auto* req = reinterpret_cast<const Msg_C2S_LogoutReq*>(data);
        if (req->action != static_cast<uint8_t>(LogoutAction::RETURN_CHAR_SELECT) &&
            req->action != static_cast<uint8_t>(LogoutAction::RETURN_LOGIN))
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }

    static ValidateResult validateMove(const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_MoveReq))
            return ValidateResult::BAD_LENGTH;
        const auto* req = reinterpret_cast<const Msg_C2S_MoveReq*>(data);
        constexpr float kMaxCoord = 100000.0f;
        if (req->x < -kMaxCoord || req->x > kMaxCoord ||
            req->y < -kMaxCoord || req->y > kMaxCoord ||
            req->z < -kMaxCoord || req->z > kMaxCoord)
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }

    static ValidateResult validateChat(const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_Chat))
            return ValidateResult::BAD_LENGTH;
        const auto* req = reinterpret_cast<const Msg_C2S_Chat*>(data);
        if (req->channel > 3)
            return ValidateResult::BAD_PAYLOAD;
        if (req->content[0] == '\0')
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }
};
