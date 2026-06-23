/**
 * @file    LoginClientMsgValidator.h
 * @brief   登录服客户端消息白名单与包长校验
 */

#pragma once

#include "ClientCommon.pb.h"
#include "LoginCommon.pb.h"
#include "ZoneCommon.pb.h"
#include <cstdint>

/** @brief 登录服客户端消息校验结果 */
enum class LoginClientValidateResult : uint8_t
{
    OK = 0,
    UNKNOWN_MSG,
    BAD_LENGTH,
};

/**
 * @brief 登录服客户端上行校验（仅 ClientListen 口）
 */
class LoginClientMsgValidator
{
public:
    static LoginClientValidateResult check(uint8_t module, uint8_t sub,
                                           const char* data, uint16_t len)
    {
        (void)data;
        constexpr uint8_t kLoginModule = static_cast<uint8_t>(rpg::client::LOGIN);

        if (module == kLoginModule)
        {
            if (sub == static_cast<uint8_t>(rpg::login::C2S_LOGIN_REQ))
                return len > 0 && len <= 4096 ? LoginClientValidateResult::OK
                                                : LoginClientValidateResult::BAD_LENGTH;
            if (sub == static_cast<uint8_t>(rpg::login::C2S_REGISTER_REQ))
                return len > 0 && len <= 4096 ? LoginClientValidateResult::OK
                                                : LoginClientValidateResult::BAD_LENGTH;
            if (sub == static_cast<uint8_t>(rpg::zone::C2S_ZONE_LIST_REQ))
                return len <= 256 ? LoginClientValidateResult::OK
                                  : LoginClientValidateResult::BAD_LENGTH;
            return LoginClientValidateResult::UNKNOWN_MSG;
        }

        return LoginClientValidateResult::UNKNOWN_MSG;
    }
};
