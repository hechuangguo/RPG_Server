# Lua 脚本体系（SceneServer）

SceneServer 内嵌 **Lua 5.4**，C++ 负责网络/调度/存档触发，Lua 承载玩法逻辑。

入口：[`script/scene/init.lua`](../script/scene/init.lua)  
管理器：[`SceneServer/LuaManager.*`](../SceneServer/LuaManager.cpp)  
绑定：[`SceneServer/ScriptFun.cpp`](../SceneServer/ScriptFun.cpp)

目录说明见 [script/README.md](../script/README.md)；配表见 [basefile/README.md](../basefile/README.md)。

---

## 1. 加载路径

`LuaManager::init()` 设置 `package.path`：

```
script/?.lua;script/?/init.lua;database/?.lua;basefile/?.lua
```

（含 `../` 回退，便于从 `.build/bin` 启动）

---

## 2. C++ → Lua 回调

| 全局函数 | 触发时机 | 参数 |
|----------|----------|------|
| `OnTick(nowMs)` | 1s 定时器 | 当前毫秒时间戳 |
| `OnUserEnter(userID, mapID)` | 用户进入场景 | int, int |
| `OnUserLeave(userID)` | 用户离开 | int |
| `OnNpcTalk(npc, userId, dialogStep, templateId)` | NPC 对话（`callScriptBool` on npc entry） | SceneEntry userdata + ints |

### C++ 客户端消息（经 MsgIngress）

Gateway 校验后，Scene 侧由 `SceneClientMsgRegister` + `ClientMsgDispatcher` 分发（**非** `handleClientMsg` / `OnMsg_*` 回退）：

| module/sub | 处理 |
|------------|------|
| 0x01/0x01 | 移动 → AOI |
| 0x05/0x01 | 地图聊天广播 |
| 0x08/0x01 | NPC 对话 → `OnNpcTalk` |

`skill_mgr.lua`、`OnSkillReq`、`OnMsg_{MMSS}` 等 **未接线**；新增 C2S 须在对应 `Common/*Msg.proto` + Validator + Router + `SceneClientMsgRegister`（或 Session，见 [SERVERS.md](SERVERS.md)）登记。

---

## 3. Lua → C++ API

### 3.1 全局函数

| 函数 | 说明 |
|------|------|
| `log_info(msg)` | 写 LOG_INFO，前缀 `[脚本]` |
| `send_to_user(userId, msgId, data)` | 经 Gateway 下发客户端（flat msgId + binary body） |
| `send_npc_talk_rsp(userId, npcId, dialogStep, text, optionsTable?)` | 下发 `S2C_NPC_TALK_RSP` |

新增绑定：在 `ScriptFun.cpp` 使用 `LUA_GLOBAL_FUNC` 宏。

### 3.2 SceneEntry 方法（userdata）

NPC 等 `SceneEntry` 实例在 Lua 中可用：

| 方法 | 返回值 |
|------|--------|
| `getEntryId()` | int |
| `getEntryType()` | int |
| `getName()` | string |
| `getLevel()` | int |
| `getHp()` / `getMaxHp()` | int |
| `getMapId()` | int |
| `getPos()` | x, y, z |
| `setHp(hp)` | — |
| `setPos(x, y, z)` | — |

元表名：`SceneEntry`（`LUA_SCENE_ENTRY_MT`）

---

## 4. init.lua 模块链

```
init.lua
  ├── data_table          (basefile)
  ├── scene.event_system
  ├── scene.npc_mgr       → npc_config
  ├── scene.skill_mgr     (硬编码 SKILL_CONFIG)
  ├── scene.entry_api     (示例，C++ 未调用)
  ├── scene.npc_dialog    → npc/*.lua
  └── quest.quest_mgr     → quest_config
```

### 4.1 EventSystem（`scene/event_system.lua`）

发布/订阅 + 延迟事件；`EventSystem.Update(nowMs)` 在 `OnTick` 中调用。

### 4.2 NpcMgr（`scene/npc_mgr.lua`）

从 `npc_config` 加载 NPC 模板，管理地图内 NPC 实例；玩家进出触发 `OnPlayerEnter/Leave`。

### 4.3 SkillMgr（`scene/skill_mgr.lua`）

脚本与 `SKILL_CONFIG` 存在，**C++ 未分发** `C2S_SKILL_REQ`；落地技能玩法须在 Validator + Router + `SceneClientMsgRegister` 接线。

### 4.4 NpcDialog（`scene/npc_dialog.lua`）

`templateId → require 路径` 映射（`BY_TEMPLATE`），桥接 C++ `OnNpcTalk` 与 per-NPC 脚本。

当前映射：`[1] = "npc.guide"` → [`script/npc/guide.lua`](../script/npc/guide.lua)

### 4.5 QuestMgr（`script/quest/quest_mgr.lua`）

任务状态机，数据来自 `quest_config`。

---

## 5. 配表使用

```lua
local npcTable = DataTable.load("npc_config")
local row = DataTable.getById(npcTable, templateId)
```

详见 [basefile/README.md](../basefile/README.md)、[DATA.md](DATA.md)。

---

## 6. 扩展指南

### 6.1 新客户端消息（Scene 侧）

1. 在 Gateway 登记 Validator + Router（见 [DEVELOPMENT.md](DEVELOPMENT.md)）
2. 在 `SceneClientMsgRegister.cpp` 注册 handler，或扩展 `SceneServer` 成员方法 + `registerClient` / `registerClientRawU32`
3. 需 Lua 时由 C++ handler 调用 `LuaManager::callScript*`（**无** `OnMsg_*` 自动回退）

### 6.2 新 NPC 对话脚本

1. 在 `Common/DataDoc` / `npc_config` 中设置 `templateId`
2. 新建 `script/npc/your_npc.lua`，实现 `OnTalk(userId, npcId, step) → { text, options }`
3. 在 `npc_dialog.lua` 的 `BY_TEMPLATE` 注册 `templateId → "npc.your_npc"`

### 6.3 新 C++ 绑定

1. `ScriptFun.cpp` 添加 `LUA_GLOBAL_FUNC` 或 `LUA_ENTRY_FUNC`
2. 在 `ScriptFun::registerAll` 中自动注册（经 `LuaBinder::install`）

---

## 7. 已知缺口

| 项 | 状态 |
|----|------|
| `script/global/global_mgr.lua` | 存在但未 `require`，无 C++ 钩子 |
| `script/scene/entry_api.lua` | 已加载，函数为示例，C++ 未调用 |
| `goblin_ai.lua` / `goblin_boss.lua` | `npc_config` / gen_datadoc 引用但**文件缺失** |
| 技能配表 | 硬编码在 `skill_mgr.lua`，未纳入 Common/DataDoc |
| Session 社交/任务 | 走 SessionServer，Lua 未参与 |

---

## 8. 调试

- Lua 日志：`log_info(...)` → 服务器 `logs/scene.log`
- 启动 cwd 须使 `script/`、`database/` 可访问（从项目根 `./RunServer.sh`）
- 改脚本后需重启 SceneServer（无热更流程）
