/**
 * @file    ClientMsgValidator.h
 * @brief  Gateway 客户端消息安全校验（白名单、长度、状态、userID）
 */

#pragma once

#include "ClientCommon.pb.h"
#include "../sdk/net/ClientProtoWire.h"
#include "../sdk/util/LoginSpawnConfig.h"
#include "../sdk/util/RoleNameUtil.h"
#include "GatewayUser.h"
#include <cstdint>
#include <cstring>

/** @brief 校验结果 */
enum class ValidateResult : uint8_t
{
    OK = 0,
    UNKNOWN_MSG,
    BAD_LENGTH,
    BAD_STATE,
    BAD_PAYLOAD,
    RATE_LIMITED,
};

/**
 * @brief 客户端消息校验器（查表 + 状态机；body 均为 Protobuf）
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
        if (rule->needsInWorld && user->getClientState() != ClientState::IN_WORLD)
            return ValidateResult::BAD_STATE;

        if (module == kLoginModule &&
            sub == static_cast<uint8_t>(rpg::login::C2S_GATEWAY_AUTH_REQ))
            return validateGatewayAuth(data, len);
        if (module == kLoginModule &&
            sub == static_cast<uint8_t>(rpg::login::C2S_CREATE_USER_REQ))
            return validateCreateUser(data, len);
        if (module == kLoginModule &&
            sub == static_cast<uint8_t>(rpg::login::C2S_LOGOUT_REQ))
            return validateLogout(data, len);
        if (module == kSceneModule &&
            sub == static_cast<uint8_t>(rpg::mapdata::C2S_MOVE_REQ))
            return validateMove(user, data, len);
        if (module == kChatModule &&
            sub == static_cast<uint8_t>(rpg::chat::C2S_CHAT_REQ))
            return validateChat(data, len);
        if (module == kNpcModule &&
            sub == static_cast<uint8_t>(rpg::npc::C2S_NPC_TALK_REQ))
            return validateNpcTalk(user, data, len);
        return ValidateResult::OK;
    }

    static int32_t toErrorCode(ValidateResult result)
    {
        switch (result)
        {
        case ValidateResult::OK:
            return static_cast<int32_t>(rpg::system::GATEWAY_VALIDATE_OK);
        case ValidateResult::UNKNOWN_MSG:
            return static_cast<int32_t>(rpg::system::GATEWAY_VALIDATE_UNKNOWN_MSG);
        case ValidateResult::BAD_LENGTH:
            return static_cast<int32_t>(rpg::system::GATEWAY_VALIDATE_BAD_LENGTH);
        case ValidateResult::BAD_STATE:
            return static_cast<int32_t>(rpg::system::GATEWAY_VALIDATE_BAD_STATE);
        case ValidateResult::BAD_PAYLOAD:
            return static_cast<int32_t>(rpg::system::GATEWAY_VALIDATE_BAD_PAYLOAD);
        case ValidateResult::RATE_LIMITED:
            return static_cast<int32_t>(rpg::system::GATEWAY_VALIDATE_RATE_LIMITED);
        }
        return static_cast<int32_t>(rpg::system::GATEWAY_VALIDATE_BAD_PAYLOAD);
    }

private:
    static constexpr uint8_t kLoginModule  = static_cast<uint8_t>(rpg::client::LOGIN);
    static constexpr uint8_t kSceneModule = static_cast<uint8_t>(rpg::client::SCENE);
    static constexpr uint8_t kChatModule   = static_cast<uint8_t>(rpg::client::CHAT);
    static constexpr uint8_t kNpcModule    = static_cast<uint8_t>(rpg::client::NPC);
    static constexpr uint8_t kSystemModule = static_cast<uint8_t>(rpg::client::SYSTEM);

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
            {kLoginModule, static_cast<uint8_t>(rpg::login::C2S_GATEWAY_AUTH_REQ),
             1, CLIENT_PROTO_MAX_BODY, STATE_CONNECTED, false},
            {kLoginModule, static_cast<uint8_t>(rpg::login::C2S_SELECT_USER_REQ),
             1, CLIENT_PROTO_MAX_BODY, STATE_ACCOUNT_OK, false},
            {kLoginModule, static_cast<uint8_t>(rpg::login::C2S_CREATE_USER_REQ),
             1, CLIENT_PROTO_MAX_BODY, STATE_ACCOUNT_OK, false},
            {kLoginModule, static_cast<uint8_t>(rpg::login::C2S_LOGOUT_REQ),
             1, CLIENT_PROTO_MAX_BODY, STATE_ENTERING | STATE_IN_WORLD, false},
            {kSystemModule, static_cast<uint8_t>(rpg::system::C2S_HEARTBEAT),
             1, CLIENT_PROTO_MAX_BODY,
             STATE_CONNECTED | STATE_AUTHING | STATE_ACCOUNT_OK | STATE_ENTERING | STATE_IN_WORLD,
             false},
            {kSceneModule, static_cast<uint8_t>(rpg::mapdata::C2S_MOVE_REQ),
             1, CLIENT_PROTO_MAX_BODY, STATE_IN_WORLD, true},
            {kChatModule, static_cast<uint8_t>(rpg::chat::C2S_CHAT_REQ),
             1, CLIENT_PROTO_MAX_BODY, STATE_IN_WORLD, true},
            {kNpcModule, static_cast<uint8_t>(rpg::npc::C2S_NPC_TALK_REQ),
             1, CLIENT_PROTO_MAX_BODY, STATE_IN_WORLD, true},
        };
        for (const auto& rule : kRules)
        {
            if (rule.module == module && rule.sub == sub)
                return &rule;
        }
        return nullptr;
    }

    /** @brief C2S_GATEWAY_AUTH_REQ：解析 Protobuf 并校验 account/token 非空 */
    static ValidateResult validateGatewayAuth(const char* data, uint16_t len)
    {
        rpg::login::C2SGatewayAuthReq req;
        if (!parseProto(data, len, req))
            return ValidateResult::BAD_LENGTH;
        if (req.account().empty() || req.login_token().empty())
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }

    /** @brief C2S_CREATE_USER_REQ：角色名 UTF-8 与职业/性别上限 */
    static ValidateResult validateCreateUser(const char* data, uint16_t len)
    {
        rpg::login::C2SCreateUserReq req;
        if (!parseProto(data, len, req))
            return ValidateResult::BAD_LENGTH;
        if (req.name().empty())
            return ValidateResult::BAD_PAYLOAD;
        if (!isValidRoleNameUtf8(req.name().c_str()))
            return ValidateResult::BAD_PAYLOAD;
        if (req.vocation() > MAX_VOCATION_ID || req.sex() > MAX_SEX_ID)
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }

    /** @brief C2S_LOGOUT_REQ：action 须为回选角或回登录 */
    static ValidateResult validateLogout(const char* data, uint16_t len)
    {
        rpg::login::C2SLogoutReq req;
        if (!parseProto(data, len, req))
            return ValidateResult::BAD_LENGTH;
        if (req.action() != rpg::login::RETURN_CHAR_SELECT &&
            req.action() != rpg::login::RETURN_LOGIN)
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }

    /** @brief C2S_MOVE_REQ：userId 与坐标边界 */
    static ValidateResult validateMove(const GatewayUser* user, const char* data, uint16_t len)
    {
        rpg::mapdata::C2SMoveReq req;
        if (!parseProto(data, len, req))
            return ValidateResult::BAD_LENGTH;
        if (!req.has_pos())
            return ValidateResult::BAD_PAYLOAD;
        if (user->GetID() != 0 && req.user_id() != user->GetID())
            return ValidateResult::BAD_PAYLOAD;
        constexpr float kMaxCoord = 100000.0f;
        const float x = req.pos().x();
        const float y = req.pos().y();
        const float z = req.pos().z();
        if (x < -kMaxCoord || x > kMaxCoord ||
            y < -kMaxCoord || y > kMaxCoord ||
            z < -kMaxCoord || z > kMaxCoord)
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }

    /** @brief C2S_CHAT_REQ：频道与 content 长度（≤ MAX_CHAT_CONTENT_BYTES） */
    static ValidateResult validateChat(const char* data, uint16_t len)
    {
        rpg::chat::C2SChatReq req;
        if (!parseProto(data, len, req))
            return ValidateResult::BAD_LENGTH;
        if (req.channel() > rpg::chat::CHAT_CHANNEL_GUILD)
            return ValidateResult::BAD_PAYLOAD;
        if (req.content().empty() || req.content().size() > MAX_CHAT_CONTENT_BYTES)
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }

    /** @brief C2S_NPC_TALK_REQ：npcId 非零（已进世界用户） */
    static ValidateResult validateNpcTalk(const GatewayUser* user, const char* data, uint16_t len)
    {
        rpg::npc::C2SNpcTalkReq req;
        if (!parseProto(data, len, req))
            return ValidateResult::BAD_LENGTH;
        if (user->GetID() != 0 && req.npc_id() == 0)
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }
};
