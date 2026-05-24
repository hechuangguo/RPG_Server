/**
 * @file    UserWireUtil.h
 * @brief  UserBase 与 UserBaseWire 互转（跨服存档协议）
 */

#pragma once
#include "../protocal/InternalMsg.h"
#include "UserBase.h"
#include <cstdio>
#include <cstring>

inline UserBaseWire toUserBaseWire(const UserBase& base)
{
    UserBaseWire wire{};
    wire.userID   = base.userID;
    snprintf(wire.name, sizeof(wire.name), "%s", base.name.c_str());
    wire.level    = base.level;
    wire.vocation = base.vocation;
    wire.sex      = base.sex;
    wire.mapID    = base.mapID;
    wire.posX     = base.posX;
    wire.posY     = base.posY;
    wire.posZ     = base.posZ;
    wire.hp       = base.hp;
    wire.maxHP    = base.maxHP;
    wire.mp       = base.mp;
    wire.maxMP    = base.maxMP;
    wire.gold     = base.gold;
    return wire;
}

inline void applyUserBaseWire(UserBase& base, const UserBaseWire& wire)
{
    base.userID   = wire.userID;
    base.name     = wire.name;
    base.level    = wire.level;
    base.vocation = wire.vocation;
    base.sex      = wire.sex;
    base.mapID    = wire.mapID;
    base.posX     = wire.posX;
    base.posY     = wire.posY;
    base.posZ     = wire.posZ;
    base.hp       = wire.hp;
    base.maxHP    = wire.maxHP;
    base.mp       = wire.mp;
    base.maxMP    = wire.maxMP;
    base.gold     = wire.gold;
}
