# script — 游戏 Lua 脚本

SceneServer 内嵌 Lua 的脚本根目录。完整 API 见 [docs/LUA.md](../docs/LUA.md)。

---

## 目录结构

```
script/
├── scene/
│   ├── init.lua           # C++ 加载入口
│   ├── event_system.lua   # 事件发布/订阅
│   ├── npc_mgr.lua        # NPC 实例管理
│   ├── npc_dialog.lua     # templateId → NPC 脚本路由
│   ├── skill_mgr.lua      # 技能（硬编码 SKILL_CONFIG）
│   └── entry_api.lua      # 示例回调（C++ 未调用）
├── quest/
│   └── quest_mgr.lua      # 任务状态机
├── npc/
│   └── guide.lua          # templateId=1 的对话脚本
└── global/
    └── global_mgr.lua     # 未接入（orphan）
```

`package.path` 还包含 `database/`（配表）与 `basefile/`（工具）。

---

## init.lua 加载顺序

```lua
require("data_table")
require("scene.event_system")
require("scene.npc_mgr")
require("scene.skill_mgr")
require("scene.entry_api")
require("scene.npc_dialog")
require("quest.quest_mgr")
```

C++ 通过 [`LuaManager`](../SceneServer/LuaManager.cpp) 加载 `script/scene/init.lua`。

---

## 扩展 NPC 对话

1. 在 `DataDoc` / `database/npc_config.lua` 配置 NPC 的 `id`（templateId）
2. 新建 `script/npc/your_npc.lua`：

```lua
local M = {}

function M.OnTalk(userId, npcId, step)
    if step == 0 then
        return { text = "你好！", options = { { text = "再见", next = -1 } } }
    end
    return nil
end

return M
```

3. 在 `scene/npc_dialog.lua` 注册：

```lua
NpcDialog.BY_TEMPLATE[yourTemplateId] = "npc.your_npc"
```

---

## 扩展客户端消息（Lua）

Gateway 路由到 Scene 后，若 C++ 未硬编码处理，实现全局函数：

```lua
function OnMsg_0301(connID, data)
    -- module=0x03 sub=0x01 (C2S_BAG_INFO_REQ)
end
```

命名：`OnMsg_` + 两位十六进制 module + 两位十六进制 sub。

---

## 扩展任务

1. 编辑 `DataDoc` → `./gen_data.sh` → `database/quest_config.lua`
2. 修改 `quest/quest_mgr.lua` 或挂接 `EventSystem`

---

## 已知问题

| 文件 | 说明 |
|------|------|
| `global/global_mgr.lua` | 未 require，无 C++ 钩子 |
| `goblin_ai.lua` / `goblin_boss.lua` | 配表引用但文件缺失 |
| `skill_mgr.lua` | 技能未走 DataDoc，改表需编辑 Lua |

---

## 相关文档

- [docs/LUA.md](../docs/LUA.md) — C++↔Lua API
- [basefile/README.md](../basefile/README.md) — DataTable
- [docs/DEVELOPMENT.md](../docs/DEVELOPMENT.md) — 扩展 checklist
