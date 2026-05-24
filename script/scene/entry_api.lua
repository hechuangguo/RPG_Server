-- ============================================================
--  entry_api.lua —— C++ LuaManager 带 SceneEntry 调用的示例
--
--  C++ 侧：
--    luaMgr.callScriptBool(npc, "OnEntryCheck", { LuaArg.integer(1) })
--    luaMgr.callScriptInt(npc, "OnEntryCalc", { ... })
--    luaMgr.callScriptList(npc, "OnEntryTargets", { ... })
--
--  约定：第一个参数为 SceneEntry userdata（entry:getEntryId() 等）
-- ============================================================

--- @param entry SceneEntry userdata
--- @return boolean
function OnEntryCheck(entry, flag)
    if not entry then return false end
    log_info(string.format("OnEntryCheck: id=%d flag=%d",
        entry:getEntryId(), flag or 0))
    return entry:getHp() > 0
end

--- @param entry SceneEntry userdata
--- @return number
function OnEntryCalc(entry, delta)
    return (entry:getHp() or 0) + (delta or 0)
end

--- @param entry SceneEntry userdata
--- @return table 整数列表
function OnEntryTargets(entry, radius)
    local r = radius or 10
    return { entry:getMapId(), r, entry:getEntryId() }
end
