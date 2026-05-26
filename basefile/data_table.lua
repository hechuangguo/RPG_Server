-- ============================================================
--  basefile/data_table.lua  —— 策划表加载与查询工具
--
--  配合 database/*.lua（由 DataDoc Excel 生成）使用：
--    local npcTable = DataTable.load("npc_config")
--    local row = DataTable.getById(npcTable, 1)
-- ============================================================

DataTable = DataTable or {}

--- 已加载模块缓存，避免重复 require
local _cache = {}

--- 加载 database 目录下的配表模块（如 "npc_config"）
--- @return table|nil  主键索引表 { [id] = row, ... }
function DataTable.load(moduleName)
    if not moduleName or moduleName == "" then
        return nil
    end
    if _cache[moduleName] then
        return _cache[moduleName]
    end
    local ok, data = pcall(require, moduleName)
    if not ok or type(data) ~= "table" then
        log_info(string.format("[DataTable] load failed: %s (%s)",
            tostring(moduleName), tostring(data)))
        return nil
    end
    _cache[moduleName] = data
    return data
end

--- 按主键取一行
function DataTable.getById(tbl, id)
    if not tbl or id == nil then
        return nil
    end
    return tbl[id]
end

--- 遍历全部行；fn(id, row) 返回 false 可提前结束
function DataTable.forEach(tbl, fn)
    if not tbl or type(fn) ~= "function" then
        return
    end
    for id, row in pairs(tbl) do
        if fn(id, row) == false then
            break
        end
    end
end

--- 筛选 mapID 等字段相同的行，返回列表
function DataTable.filter(tbl, fieldName, fieldValue)
    local out = {}
    if not tbl then
        return out
    end
    for id, row in pairs(tbl) do
        if row[fieldName] == fieldValue then
            out[#out + 1] = row
        end
    end
    return out
end

--- 清空缓存（热更配表前可调用）
function DataTable.clearCache()
    for name in pairs(_cache) do
        package.loaded[name] = nil
        _cache[name] = nil
    end
end

return DataTable
