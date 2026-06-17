/**
 * @file    LoginTokenUtil.cpp
 * @brief   LoginServer 登录票据生成实现
 */

#include "LoginTokenUtil.h"

#include "../sdk/math/Random.h"

#include <cstdio>
#include <cstring>

bool generateLoginToken(char* out, size_t outLen)
{
    if (!out || outLen < 65)
        return false;
    for (int i = 0; i < 32; ++i)
    {
        const uint8_t b = static_cast<uint8_t>(Random::range(0, 255));
        std::snprintf(out + i * 2, 3, "%02x", b);
    }
    out[64] = '\0';
    return true;
}
