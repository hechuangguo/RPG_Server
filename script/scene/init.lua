-- ============================================================
--  script/scene/init.lua  —— SceneServer Lua 初始化入口
--  负责：注册全局tick回调、用户进出场景、技能请求分发
--  C++ 通过此模块调用 Lua 逻辑，实现场景核心流程调度
-- ============================================================

-- 配表工具（basefile/data_table.lua）
require("data_table")

-- 引入框架模块
require("scene.event_system")
require("scene.npc_mgr")
require("scene.skill_mgr")
require("scene.entry_api")
require("scene.npc_dialog")
require("quest.quest_mgr")

print("[脚本] scene/init.lua loaded")

-- ============================================================
--  全局 tick（每秒被 C++ 调用）
-- ============================================================
function OnTick(nowMs)
    EventSystem.Update(nowMs)
    NpcMgr.Update(nowMs)
end

-- ============================================================
--  用户进入场景
-- ============================================================
function OnUserEnter(userID, mapID)
    log_info(string.format("OnUserEnter: userID=%d mapID=%d", userID, mapID))
    -- 触发事件
    EventSystem.Fire("user_enter", userID, mapID)
    -- 通知 NPC 管理器
    NpcMgr.OnPlayerEnter(userID, mapID)
end

-- ============================================================
--  用户离开场景
-- ============================================================
function OnUserLeave(userID)
    log_info(string.format("OnUserLeave: userID=%d", userID))
    EventSystem.Fire("user_leave", userID)
    NpcMgr.OnPlayerLeave(userID)
end

-- ============================================================
--  技能请求（C++ OnSkillReq → Lua）
-- ============================================================
function OnSkillReq(connID, rawData)
    log_info(string.format("OnSkillReq: connID=%d dataLen=%d", connID, #rawData))
    SkillMgr.HandleSkillReq(connID, rawData)
end
