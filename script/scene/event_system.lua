-- ============================================================
--  script/scene/event_system.lua  —— Lua 事件系统
--  采用发布/订阅（Pub-Sub）模式：
--    发布者(Fire) 按事件名广播消息，无需知晓订阅者
--    订阅者(On)   按事件名注册回调，在事件触发时被调用
--    取消订阅(Off) 移除已注册的处理器
--  实现模块间松耦合通信
-- ============================================================

EventSystem = {}

local _listeners = {}   -- { eventName -> {handler1, handler2, ...} }

-- 注册事件监听
function EventSystem.On(eventName, handler)
    if not _listeners[eventName] then
        _listeners[eventName] = {}
    end
    table.insert(_listeners[eventName], handler)
end

-- 取消监听
function EventSystem.Off(eventName, handler)
    local list = _listeners[eventName]
    if not list then return end
    for i = #list, 1, -1 do
        if list[i] == handler then
            table.remove(list, i)
        end
    end
end

-- 触发事件
function EventSystem.Fire(eventName, ...)
    local list = _listeners[eventName]
    if not list then return end
    for _, handler in ipairs(list) do
        local ok, err = pcall(handler, ...)
        if not ok then
            log_info("[EventSystem] Error in handler for '" .. eventName .. "': " .. tostring(err))
        end
    end
end

-- ============================================================
--  延迟事件机制（FireAfter）：
--    将事件加入定时队列，在指定毫秒后自动触发
--    参数：delayMs(延迟毫秒), eventName(事件名), ...(事件参数)
--    由 EventSystem.Update(nowMs) 每帧检查到期事件并执行
-- ============================================================
local _timedEvents = {}

function EventSystem.FireAfter(delayMs, eventName, ...)
    local args = {...}
    table.insert(_timedEvents, {
        fireAt = os.clock() * 1000 + delayMs,
        name   = eventName,
        args   = args,
    })
end

function EventSystem.Update(nowMs)
    for i = #_timedEvents, 1, -1 do
        local ev = _timedEvents[i]
        if nowMs >= ev.fireAt then
            EventSystem.Fire(ev.name, table.unpack(ev.args))
            table.remove(_timedEvents, i)
        end
    end
end

-- 预定义事件示例：用户进入地图时发广播
EventSystem.On("user_enter", function(userID, mapID)
    log_info(string.format("[Event] user_enter: user=%d map=%d", userID, mapID))
end)

EventSystem.On("user_leave", function(userID)
    log_info(string.format("[Event] user_leave: user=%d", userID))
end)
