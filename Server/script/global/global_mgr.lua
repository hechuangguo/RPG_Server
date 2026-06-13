-- ============================================================
--  script/global/global_mgr.lua  —— 全区全局数据脚本层
-- ============================================================

GlobalMgr = {}

-- 全服公告队列
local _notices = {}

-- 排行榜缓存
local _rankCache = {}

-- 全服广播公告
function GlobalMgr.BroadcastNotice(msg, level)
    level = level or "info"
    table.insert(_notices, { msg=msg, level=level, time=os.time() })
    log_info(string.format("[GlobalMgr] Notice[%s]: %s", level, msg))
    -- 实际：通过 C++ 接口广播给所有 GatewayServer
end

-- ============================================================
--  排行榜排序算法：
--    1. 查找用户是否已存在 → 存在则更新 value
--    2. 不存在则插入新条目 {userID, name, value}
--    3. table.sort 按 value 降序排列
--    4. 截取前 100 名（防止无限增长）
-- ============================================================
function GlobalMgr.UpdateRank(rankType, userID, name, value)
    if not _rankCache[rankType] then _rankCache[rankType] = {} end
    local rank = _rankCache[rankType]
    -- 查找是否已存在
    local found = false
    for i, entry in ipairs(rank) do
        if entry.userID == userID then
            entry.value = value
            found = true
            break
        end
    end
    if not found then
        table.insert(rank, { userID=userID, name=name, value=value })
    end
    -- 排序（降序）
    table.sort(rank, function(a, b) return a.value > b.value end)
    -- 取前 100
    while #rank > 100 do table.remove(rank) end
    log_info(string.format("[GlobalMgr] RankUpdate: type=%s user=%d value=%d", rankType, userID, value))
end

-- 查询排行榜前 N 名
function GlobalMgr.GetRank(rankType, topN)
    topN = topN or 10
    local rank = _rankCache[rankType] or {}
    local result = {}
    for i = 1, math.min(topN, #rank) do
        result[i] = rank[i]
    end
    return result
end

-- ============================================================
--  日榜重置逻辑：
--    每天零点调用 ResetDailyRank
--    将 daily_kill(击杀榜) 和 daily_exp(经验榜) 清空
--    需配合 Cron 定时器或 C++ 层定时回调触发
-- ============================================================
function GlobalMgr.ResetDailyRank()
    _rankCache["daily_kill"]  = {}
    _rankCache["daily_exp"]   = {}
    log_info("[GlobalMgr] Daily rank reset.")
end

-- 监听等级变化，更新等级榜
EventSystem.On("user_level_up", function(userID, name, newLevel)
    GlobalMgr.UpdateRank("level", userID, name, newLevel)
end)
