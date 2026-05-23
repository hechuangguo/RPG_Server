#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <vector>

// ============================================================
//  消息分发器（MsgID → Handler）
// ============================================================

using MsgHandler = std::function<void(uint32_t connID, const char* data, uint16_t len)>;

class MsgDispatcher
{
public:
    static MsgDispatcher& Instance()
    {
        static MsgDispatcher s;
        return s;
    }

    void Register(uint16_t msgID, MsgHandler handler)
    {
        m_handlers[msgID] = std::move(handler);
    }

    bool Dispatch(uint32_t connID, uint16_t msgID, const char* data, uint16_t len)
    {
        auto it = m_handlers.find(msgID);
        if (it == m_handlers.end()) return false;
        it->second(connID, data, len);
        return true;
    }

private:
    std::unordered_map<uint16_t, MsgHandler> m_handlers;
};

// ============================================================
//  Lua 绑定辅助（前置声明）
// ============================================================
struct lua_State;
extern "C"
{
    #include "lua.h"
    #include "lualib.h"
    #include "lauxlib.h"
}

namespace LuaHelper
{
    // 安全调用 Lua 函数
    inline bool CallFunc(lua_State* L, const char* funcName, int nargs, int nret)
    {
        lua_getglobal(L, funcName);
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
        // 将参数从调用前压入（调用者负责）
        if (lua_pcall(L, nargs, nret, 0) != LUA_OK)
        {
            fprintf(stderr, "[Lua] Error calling %s: %s\n",
                    funcName, lua_tostring(L, -1));
            lua_pop(L, 1);
            return false;
        }
        return true;
    }
}
