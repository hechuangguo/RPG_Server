/**
 * @file    MsgHandlerBinder.h
 * @brief  MsgDispatcher / ClientMsgDispatcher 成员函数注册辅助
 */

#pragma once

#include "MsgDispatcher.h"
#include "ClientMsgDispatcher.h"
#include <cstdint>

/**
 * @brief 注册区内消息（raw 签名，无自动长度检查）
 */
template<typename Server>
void registerInternalRaw(MsgDispatcher& d, Server* server, uint16_t msgId,
                         void (Server::*fn)(ConnID, const char*, uint16_t))
{
    d.Register(msgId, [server, fn](uint32_t connId, const char* data, uint16_t len) {
        (server->*fn)(static_cast<ConnID>(connId), data, len);
    });
}

/**
 * @brief 注册区内定长消息（自动 sizeof 守卫 + 引用体）
 */
template<typename Server, typename BodyT>
void registerInternal(MsgDispatcher& d, Server* server, uint16_t msgId,
                      void (Server::*fn)(ConnID, const BodyT&))
{
    d.Register(msgId, [server, fn](uint32_t connId, const char* data, uint16_t len) {
        if (len < sizeof(BodyT))
            return;
        (server->*fn)(static_cast<ConnID>(connId),
                      *reinterpret_cast<const BodyT*>(data));
    });
}

/**
 * @brief 注册区内消息：自由函数 (Server&, ConnID, raw body)
 */
template<typename Server>
void registerInternalFree(MsgDispatcher& d, Server& server, uint16_t msgId,
                          void (*fn)(Server&, ConnID, const char*, uint16_t))
{
    d.Register(msgId, [&server, fn](uint32_t connId, const char* data, uint16_t len) {
        fn(server, static_cast<ConnID>(connId), data, len);
    });
}

/**
 * @brief 注册区内定长消息：自由函数 + sizeof 守卫 + const BodyT&
 */
template<typename Server, typename BodyT>
void registerInternalFree(MsgDispatcher& d, Server& server, uint16_t msgId,
                          void (*fn)(Server&, ConnID, const BodyT&))
{
    d.Register(msgId, [&server, fn](uint32_t connId, const char* data, uint16_t len) {
        if (len < sizeof(BodyT))
            return;
        fn(server, static_cast<ConnID>(connId), *reinterpret_cast<const BodyT*>(data));
    });
}

/**
 * @brief 注册客户端消息（raw 签名）
 */
template<typename Server>
void registerClientRaw(ClientMsgDispatcher& d, Server* server,
                      uint8_t module, uint8_t sub,
                      void (Server::*fn)(ConnID, const char*, uint16_t))
{
    d.Register(module, sub, [server, fn](uint32_t connId, const char* data, uint16_t len) {
        (server->*fn)(static_cast<ConnID>(connId), data, len);
    });
}

/**
 * @brief 注册客户端定长消息（BodyT 须含 kModule、kSub）
 */
template<typename Server, typename BodyT>
void registerClient(ClientMsgDispatcher& d, Server* server,
                    void (Server::*fn)(ConnID, const BodyT&))
{
    d.Register(BodyT::kModule, BodyT::kSub,
               [server, fn](uint32_t connId, const char* data, uint16_t len) {
                   if (len < sizeof(BodyT))
                       return;
                   (server->*fn)(static_cast<ConnID>(connId),
                                 *reinterpret_cast<const BodyT*>(data));
               });
}
