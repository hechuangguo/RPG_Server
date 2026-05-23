-- ============================================================
--  script/scene/event_system.lua  —— Lua 事件系统
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

-- 定时触发队列（延迟事件）
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

-- 预定义事件示例：角色进入地图时发广播
EventSystem.On("role_enter", function(roleID, mapID)
    log_info(string.format("[Event] role_enter: role=%d map=%d", roleID, mapID))
end)

EventSystem.On("role_leave", function(roleID)
    log_info(string.format("[Event] role_leave: role=%d", roleID))
end)
