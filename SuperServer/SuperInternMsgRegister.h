/**
 * @file    SuperInternMsgRegister.h
 * @brief  超级服区内 S2S 消息注册聚合
 */

#pragma once

class SuperServer;

/** @brief 注册超级服区内消息及外联/登录等模块化处理器 */
void SuperInternMsgRegister(SuperServer& server);
