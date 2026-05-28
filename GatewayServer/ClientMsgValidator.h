/**
 * @file    ClientMsgValidator.h
 * @brief  Gateway 客户端消息安全校验（白名单、长度、状态、userID）
 */

#pragma once
#include "../common/ClientMsg.h"
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

        if (rule->needsLoggedIn && user->getClientState() != ClientState::LOGGED_IN)
            return ValidateResult::BAD_STATE;

        if (rule->checkUserId && len >= static_cast<uint16_t>(sizeof(uint64_t)))
        {
            uint64_t packetUserId = 0;
            memcpy(&packetUserId, data, sizeof(uint64_t));
            if (user->GetID() != 0 && packetUserId != user->GetID())
                return ValidateResult::BAD_PAYLOAD;
        }

        if (module == static_cast<uint8_t>(ClientModule::SCENE) && sub == 0x01)
            return validateMove(data, len);

        if (module == static_cast<uint8_t>(ClientModule::CHAT) && sub == 0x01)
            return validateChat(data, len);

        if (module == static_cast<uint8_t>(ClientModule::LOGIN) && sub == 0x01)
            return validateLogin(data, len);

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
    static constexpr uint8_t STATE_CONNECTED = 1u << 0; /**< 已连接未登录 */
    static constexpr uint8_t STATE_LOGGING   = 1u << 1; /**< 登录校验中 */
    static constexpr uint8_t STATE_LOGGED_IN = 1u << 2; /**< 已登录可进游戏 */

    struct MsgRule
    {
        uint8_t  module;        /**< ClientModule 编号 */
        uint8_t  sub;           /**< 子协议号 */
        uint16_t minLen;        /**< 允许最小包体长度 */
        uint16_t maxLen;        /**< 允许最大包体长度 */
        uint8_t  allowedStates; /**< 位掩码形式的允许连接状态 */
        bool     needsLoggedIn; /**< 是否要求 LOGGED_IN 状态 */
        bool     checkUserId;   /**< 是否校验包内 userId 与会话一致 */
    };

    static bool isStateAllowed(ClientState state, uint8_t mask)
    {
        uint8_t bit = 0; /**< 当前状态映射到掩码位 */
        switch (state)
        {
        case ClientState::CONNECTED: bit = STATE_CONNECTED; break;
        case ClientState::LOGGING:   bit = STATE_LOGGING; break;
        case ClientState::LOGGED_IN: bit = STATE_LOGGED_IN; break;
        }
        return (mask & bit) != 0;
    }

    static const MsgRule* findRule(uint8_t module, uint8_t sub)
    {
        static const MsgRule kRules[] = {
            {0x00, 0x01, sizeof(Msg_C2S_LoginReq), sizeof(Msg_C2S_LoginReq),
             STATE_CONNECTED, false, false},
            {0x00, 0x03, 32, 128, STATE_CONNECTED | STATE_LOGGED_IN, false, false},
            {0x00, 0x05, 8, 64, STATE_LOGGED_IN, true, false},
            {0x0F, 0x01, sizeof(Msg_C2S_Heartbeat), sizeof(Msg_C2S_Heartbeat),
             STATE_CONNECTED | STATE_LOGGING | STATE_LOGGED_IN, false, false},
            {0x01, 0x01, sizeof(Msg_C2S_MoveReq), sizeof(Msg_C2S_MoveReq),
             STATE_LOGGED_IN, true, true},
            {0x01, 0x07, 16, 64, STATE_LOGGED_IN, true, true},
            {0x02, 0x01, 16, 128, STATE_LOGGED_IN, true, true},
            {0x04, 0x01, 8, 256, STATE_LOGGED_IN, true, true},
            {0x05, 0x01, sizeof(Msg_C2S_Chat), sizeof(Msg_C2S_Chat),
             STATE_LOGGED_IN, true, false},
            {0x05, 0x03, 16, 320, STATE_LOGGED_IN, true, false},
            {0x06, 0x01, 8, 64, STATE_LOGGED_IN, true, false},
            {0x06, 0x10, 8, 64, STATE_LOGGED_IN, true, false},
            {0x07, 0x01, 8, 32, STATE_LOGGED_IN, true, true},
            {0x07, 0x03, 8, 32, STATE_LOGGED_IN, true, true},
            {0x08, 0x01, 8, 64, STATE_LOGGED_IN, true, true},
        };
        for (const auto& rule : kRules)
        {
            if (rule.module == module && rule.sub == sub)
                return &rule;
        }
        return nullptr;
    }

    static ValidateResult validateLogin(const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_LoginReq))
            return ValidateResult::BAD_LENGTH;
        const auto* req = reinterpret_cast<const Msg_C2S_LoginReq*>(data);
        if (req->account[0] == '\0')
            return ValidateResult::BAD_PAYLOAD;
        return ValidateResult::OK;
    }

    static ValidateResult validateMove(const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_MoveReq))
            return ValidateResult::BAD_LENGTH;
        const auto* req = reinterpret_cast<const Msg_C2S_MoveReq*>(data);
        constexpr float kMaxCoord = 100000.0f; /**< 坐标合法范围绝对值上限 */
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
