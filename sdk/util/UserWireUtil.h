/**
 * @file    UserWireUtil.h
 * @brief  UserBase 与 UserBaseWire 互转（跨服存档协议）
 */

#pragma once
#include "../protocal/InternalMsg.h"
#include "UserBase.h"
#include "WireStringUtil.h"
#include <cstring>

/**
 * @brief UserBase 转为 UserBaseWire（用于服间二进制传输）
 * @param base 业务层用户基础数据
 * @return 可直接写入协议体的定长结构
 */
inline UserBaseWire toUserBaseWire(const UserBase& base)
{
    UserBaseWire wire{};
    wire.userID   = base.userID;
    copyToWire(wire.name, sizeof(wire.name), base.name.c_str());
    wire.level    = base.level;
    wire.vocation = base.vocation;
    wire.sex      = base.sex;
    wire.modelID  = base.modelID;
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

/**
 * @brief 用 UserBaseWire 覆盖 UserBase 字段
 * @param base [in,out] 目标业务对象
 * @param wire 协议层解码后的定长结构
 */
inline void applyUserBaseWire(UserBase& base, const UserBaseWire& wire)
{
    base.userID   = wire.userID;
    base.name     = wire.name;
    base.level    = wire.level;
    base.vocation = wire.vocation;
    base.sex      = wire.sex;
    base.modelID  = wire.modelID;
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
