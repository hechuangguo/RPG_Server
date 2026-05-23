-- ============================================================
--  script/quest/quest_mgr.lua  —— 任务管理器
-- ============================================================

QuestMgr = {}

-- 任务配置
local QUEST_CONFIG = {
    [1001] = {
        id      = 1001,
        name    = "初出茅庐",
        desc    = "消灭 5 只哥布林，收集耳朵",
        type    = "kill",
        target  = 3,     -- NPC ID（哥布林）
        count   = 5,
        reward  = { exp=500, gold=100, itemID=2001, itemCount=1 },
    },
    [1002] = {
        id      = 1002,
        name    = "铁匠的委托",
        desc    = "收集 10 块铁矿石",
        type    = "collect",
        itemID  = 5001,
        count   = 10,
        reward  = { exp=800, gold=200, itemID=3001, itemCount=1 },
    },
}

-- 角色任务进度 { roleID -> { questID -> progress } }
local _progress = {}

function QuestMgr.Accept(roleID, questID)
    local cfg = QUEST_CONFIG[questID]
    if not cfg then return false, "任务不存在" end
    if not _progress[roleID] then _progress[roleID] = {} end
    if _progress[roleID][questID] then return false, "任务已接受" end
    _progress[roleID][questID] = { questID=questID, current=0, target=cfg.count, done=false }
    log_info(string.format("[QuestMgr] role=%d accepted quest=%d '%s'", roleID, questID, cfg.name))
    return true, "OK"
end

function QuestMgr.UpdateProgress(roleID, questID, delta)
    local prog = _progress[roleID] and _progress[roleID][questID]
    if not prog or prog.done then return end
    prog.current = prog.current + delta
    local cfg = QUEST_CONFIG[questID]
    if prog.current >= prog.target then
        prog.done = true
        log_info(string.format("[QuestMgr] role=%d quest=%d '%s' COMPLETED!", roleID, questID, cfg.name))
        EventSystem.Fire("quest_complete", roleID, questID)
    end
end

function QuestMgr.Submit(roleID, questID)
    local prog = _progress[roleID] and _progress[roleID][questID]
    if not prog then return false, "未接受该任务" end
    if not prog.done then return false, "任务未完成" end
    local cfg = QUEST_CONFIG[questID]
    -- 发放奖励
    log_info(string.format("[QuestMgr] role=%d submit quest=%d reward: exp=%d gold=%d",
             roleID, questID, cfg.reward.exp, cfg.reward.gold))
    EventSystem.Fire("quest_reward", roleID, cfg.reward)
    _progress[roleID][questID] = nil
    return true, "OK"
end

-- 监听 NPC 击杀事件，更新击杀类任务进度
EventSystem.On("npc_die", function(npcID, killerID)
    if not _progress[killerID] then return end
    for questID, prog in pairs(_progress[killerID]) do
        local cfg = QUEST_CONFIG[questID]
        if cfg and cfg.type == "kill" and cfg.target == npcID then
            QuestMgr.UpdateProgress(killerID, questID, 1)
        end
    end
end)

-- 监听物品收集事件
EventSystem.On("item_collect", function(roleID, itemID, count)
    if not _progress[roleID] then return end
    for questID, prog in pairs(_progress[roleID]) do
        local cfg = QUEST_CONFIG[questID]
        if cfg and cfg.type == "collect" and cfg.itemID == itemID then
            QuestMgr.UpdateProgress(roleID, questID, count)
        end
    end
end)

-- 监听接受任务事件
EventSystem.On("quest_accept", function(roleID, questID)
    QuestMgr.Accept(roleID, questID)
end)
