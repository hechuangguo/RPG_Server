-- ============================================================
--  script/scene/npc_mgr.lua  —— NPC 管理器
-- ============================================================

NpcMgr = {}

-- NPC 配置表（实际项目从 database 目录加载）
local NPC_CONFIG = {
    [1] = { id=1, name="史密斯铁匠",  mapID=1002, x=100, z=200, type="npc",  dialog="npc/smith.lua"  },
    [2] = { id=2, name="新手引导官",  mapID=1001, x= 50, z= 50, type="npc",  dialog="npc/guide.lua"  },
    [3] = { id=3, name="哥布林",      mapID=2001, x=300, z=400, type="mob",  ai="npc/goblin_ai.lua"  },
    [4] = { id=4, name="精英哥布林",  mapID=2001, x=350, z=420, type="boss", ai="npc/goblin_boss.lua"},
}

-- 运行时 NPC 实例
local _npcs = {}

-- 初始化所有 NPC
function NpcMgr.Init(mapID)
    for _, cfg in pairs(NPC_CONFIG) do
        if cfg.mapID == mapID then
            local npc = {
                id      = cfg.id,
                name    = cfg.name,
                mapID   = cfg.mapID,
                x       = cfg.x,
                z       = cfg.z,
                hp      = 100,
                maxHP   = 100,
                type    = cfg.type,
                alive   = true,
                respawnTimer = 0,
            }
            _npcs[cfg.id] = npc
            log_info(string.format("[NpcMgr] Spawned: %s at (%d,%d) map=%d",
                     cfg.name, cfg.x, cfg.z, cfg.mapID))
        end
    end
end

-- 玩家进入地图时初始化该地图 NPC
function NpcMgr.OnPlayerEnter(userID, mapID)
    NpcMgr.Init(mapID)
end

function NpcMgr.OnPlayerLeave(userID)
    -- 如果地图没有玩家了，可以清理 NPC
end

-- 每帧 Update
function NpcMgr.Update(nowMs)
    for id, npc in pairs(_npcs) do
        if not npc.alive then
            -- ============================================================
            --  复活计时逻辑：
            --    死亡时设置 respawnTimer = 当前时间 + 复活间隔
            --    每帧检查：nowMs >= respawnTimer 时恢复 alive=true，重置HP
            -- ============================================================
            if nowMs >= npc.respawnTimer then
                npc.alive    = true
                npc.hp       = npc.maxHP
                log_info(string.format("[NpcMgr] %s respawned", npc.name))
            end
        else
            NpcMgr.UpdateAI(npc, nowMs)
        end
    end
end

-- ============================================================
--  简易 AI 状态机：
--    活着的战斗NPC(mob/boss)每帧检查：
--      1. 扫描附近玩家 → 选取仇恨目标
--      2. 距离判断 → 追击或返回巡逻点
--      3. 进入攻击范围 → 执行攻击
--    实际AI逻辑由外部脚本(ai字段)加载实现
-- ============================================================
function NpcMgr.UpdateAI(npc, nowMs)
    if npc.type == "mob" or npc.type == "boss" then
        -- 简单巡逻逻辑（实际可加载外部 AI 脚本）
        -- TODO: 向附近玩家发起攻击
    end
end

-- NPC 死亡
function NpcMgr.OnNpcDie(npcID, killerID)
    local npc = _npcs[npcID]
    if not npc then return end
    npc.alive        = false
    npc.respawnTimer = os.clock() * 1000 + 30000  -- 30 秒复活
    log_info(string.format("[NpcMgr] %s died, respawn in 30s", npc.name))
    -- 触发掉落事件
    EventSystem.Fire("npc_die", npcID, killerID)
end

-- 获取 NPC 信息
function NpcMgr.GetNpc(npcID)
    return _npcs[npcID]
end
