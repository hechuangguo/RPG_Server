-- ============================================================
--  script/scene/init.lua  —— SceneServer Lua 初始化入口
-- ============================================================

-- 引入框架模块
require("scene.event_system")
require("scene.npc_mgr")
require("scene.skill_mgr")

print("[Lua] scene/init.lua loaded")

-- ============================================================
--  全局 tick（每秒被 C++ 调用）
-- ============================================================
function OnTick(nowMs)
    EventSystem.Update(nowMs)
    NpcMgr.Update(nowMs)
end

-- ============================================================
--  角色进入场景
-- ============================================================
function OnRoleEnter(roleID, mapID)
    log_info(string.format("OnRoleEnter: roleID=%d mapID=%d", roleID, mapID))
    -- 触发事件
    EventSystem.Fire("role_enter", roleID, mapID)
    -- 通知 NPC 管理器
    NpcMgr.OnPlayerEnter(roleID, mapID)
end

-- ============================================================
--  角色离开场景
-- ============================================================
function OnRoleLeave(roleID)
    log_info(string.format("OnRoleLeave: roleID=%d", roleID))
    EventSystem.Fire("role_leave", roleID)
    NpcMgr.OnPlayerLeave(roleID)
end

-- ============================================================
--  技能请求（C++ OnSkillReq → Lua）
-- ============================================================
function OnSkillReq(connID, rawData)
    log_info(string.format("OnSkillReq: connID=%d dataLen=%d", connID, #rawData))
    SkillMgr.HandleSkillReq(connID, rawData)
end
