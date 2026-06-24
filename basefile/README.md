# basefile — 配表加载工具

运行时 Lua 工具，供 SceneServer 脚本加载 `database/*.lua` 策划配表。

完整数据管线见 [docs/DATA.md](../docs/DATA.md)、[database/README.md](../database/README.md)。

---

## data_table.lua

模块名：`data_table`（`require("data_table")` 即可，无需 `.lua` 后缀）

### API

| 函数 | 说明 |
|------|------|
| `DataTable.load(moduleName)` | `require` `database/<moduleName>.lua`，带缓存 |
| `DataTable.getById(tbl, id)` | 按主键取一行 |
| `DataTable.forEach(tbl, fn)` | 遍历；`fn(id, row)` 返回 false 可中断 |
| `DataTable.filter(tbl, fieldName, fieldValue)` | 按字段筛选，返回列表 |
| `DataTable.clearCache()` | 清缓存与 `package.loaded`（热更前） |

### 示例

```lua
local npcTable = DataTable.load("npc_config")
local row = DataTable.getById(npcTable, 1)
local onMap = DataTable.filter(npcTable, "mapId", 1001)

DataTable.forEach(npcTable, function(id, row)
    log_info(string.format("npc %d: %s", id, row.name or ""))
end)
```

### 配表文件位置

- 源：`Common/DataDoc/*.xlsx`
- 生成：`./gen_data.sh` → `database/*_config.lua`
- **禁止手改** AUTO-GENERATED 文件

### 当前配表

| 模块名 | 消费者 |
|--------|--------|
| `npc_config` | `script/scene/npc_mgr.lua` |
| `quest_config` | `script/quest/quest_mgr.lua` |

---

## package.path

SceneServer `LuaManager` 将 `basefile/` 加入 `package.path`，与 `script/`、`database/` 同级。

---

## 相关文档

- [docs/LUA.md](../docs/LUA.md)
- [script/README.md](../script/README.md)
- [Common/DataDoc/README.md](../Common/DataDoc/README.md)
