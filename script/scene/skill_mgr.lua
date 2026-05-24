-- ============================================================
--  script/scene/skill_mgr.lua  —— 技能管理器
-- ============================================================

SkillMgr = {}

-- ============================================================
--  技能类型枚举：
--    attack  - 物理攻击（受攻击力加成，忽略魔抗）
--    magic   - 魔法攻击（受魔攻加成，受魔抗减免）
--    heal    - 治愈术（恢复目标生命值）
--    buff    - 增益效果（施加临时状态）
-- ============================================================
local SKILL_CONFIG = {
    [1001] = { id=1001, name="普通攻击", cd=1000,  mpCost=0,  type="attack", dmgRate=1.0  },
    [1002] = { id=1002, name="重击",     cd=5000,  mpCost=20, type="attack", dmgRate=2.5  },
    [1003] = { id=1003, name="治愈术",   cd=8000,  mpCost=30, type="heal",   healRate=1.5 },
    [1004] = { id=1004, name="火球术",   cd=3000,  mpCost=25, type="magic",  dmgRate=3.0  },
    [2001] = { id=2001, name="狂暴",     cd=30000, mpCost=50, type="buff",   buffID=101   },
}

-- 用户技能冷却记录
-- CD 计算方式：nextUseTime = currentTime + skillCD
-- 每次使用前检查 now >= nextUseTime，若未到则拒绝施放
local _userCd = {}   -- { userID -> { skillID -> nextUseTime } }

function SkillMgr.HandleSkillReq(connID, rawData)
    -- rawData 格式：[skillID(2)][targetID(8)]（简化）
    if #rawData < 10 then return end
    -- 使用 string.byte 解析（实际可用 struct 模块）
    local b1, b2 = string.byte(rawData, 1, 2)
    local skillID = b1 + b2 * 256
    -- 查找使用该连接的用户（简化：connID = userID）
    local userID = connID

    local cfg = SKILL_CONFIG[skillID]
    if not cfg then
        log_info(string.format("[SkillMgr] Unknown skillID=%d", skillID))
        return
    end

    -- 检查冷却：当前时间是否已超过下次可用时间
    local now = os.clock() * 1000
    if not _userCd[userID] then _userCd[userID] = {} end
    local nextUse = _userCd[userID][skillID] or 0
    if now < nextUse then
        log_info(string.format("[SkillMgr] CD not ready: skill=%s user=%d", cfg.name, userID))
        return
    end

    -- 设置冷却：下次可用时间 = 当前时间 + 技能CD时长
    _userCd[userID][skillID] = now + cfg.cd

    -- 执行技能
    if cfg.type == "attack" or cfg.type == "magic" then
        SkillMgr.DoAttackSkill(userID, cfg, rawData)
    elseif cfg.type == "heal" then
        SkillMgr.DoHealSkill(userID, cfg)
    elseif cfg.type == "buff" then
        SkillMgr.DoBuffSkill(userID, cfg)
    end

    log_info(string.format("[SkillMgr] UseSkill: user=%d skill=%s", userID, cfg.name))
end

function SkillMgr.DoAttackSkill(userID, cfg, rawData)
    -- 解析目标 ID（bytes 3-10）
    -- 计算伤害、广播结果
    EventSystem.Fire("skill_attack", userID, cfg.id, cfg.dmgRate)
end

function SkillMgr.DoHealSkill(userID, cfg)
    EventSystem.Fire("skill_heal", userID, cfg.id, cfg.healRate)
end

function SkillMgr.DoBuffSkill(userID, cfg)
    EventSystem.Fire("skill_buff", userID, cfg.id, cfg.buffID)
end

-- 监听 NPC 死亡：结算掉落
EventSystem.On("npc_die", function(npcID, killerID)
    log_info(string.format("[SkillMgr] npc=%d killed by user=%d, process drops", npcID, killerID))
    -- 触发掉落逻辑
    EventSystem.Fire("item_drop", npcID, killerID)
end)
