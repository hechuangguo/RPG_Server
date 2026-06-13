# 数据层参考

项目使用**双轨数据**：MySQL 存玩家持久化；Excel/Lua 存静态策划表。

| 轨道 | 路径 | 说明 |
|------|------|------|
| MySQL | [`tables/`](../tables/) | 角色、社交、集群拓扑；仅 RecordServer 写游戏数据 |
| 策划 Lua | [`DataDoc/`](../DataDoc/) → [`database/`](../database/) | 静态配置，SceneServer Lua 加载 |

脚本说明见 [tables/README.md](../tables/README.md)、[database/README.md](../database/README.md)、[DataDoc/README.md](../DataDoc/README.md)。

---

## 1. MySQL 表（`tables/init.sql`）

库名默认 `rpg_game`，应用账号 `rpg_table` / `rpg_table`（与 `config/config.xml` 一致）。

### 1.1 总览

| 表 | 设计意图 | 读写进程 | 实现状态 |
|----|----------|----------|----------|
| **CharBase** | 账号+角色合并；`` `binary` `` 存 bag/skills/buffs/quests | RecordServer | **已实现** |
| **Relation** | 好友/黑名单 JSON + 社交扩展 `` `binary` `` | RecordServer ↔ SessionServer | **已实现** |
| **Friend** | 双向好友/黑名单行 | — | **仅 DDL** |
| **Mail** | 离线邮件 + 附件 | — | **仅 DDL** |
| **MapInfo** | 每用户每地图 JSON 存档 | — | **仅 DDL** |
| **ServerList** | 区内 6 服拓扑 | SuperServer 启动只读 | **已实现** |
| **ZoneInfo** | 登录区入口列表 | LoginServer `ZoneInfoStore` | **已实现** |

### 1.2 CharBase

**读写**：RecordServer（`RecordUserManager`）

| 字段 | 说明 |
|------|------|
| `user_id` | 自增主键 |
| `name` | 角色名，**全局唯一**；登录验证按 name 查找/创建 |
| `level`, `vocation`, `sex` | 基础属性 |
| `map_id`, `pos_x/y/z` | 位置 |
| `hp`, `max_hp`, `mp`, `max_mp`, `gold` | 战斗/货币 |
| `` `binary` `` | 包裹/技能/Buff/任务等序列化 blob |
| `create_time`, `update_time` | 时间戳 |

**登录语义**：Gateway → Record `REC_LOGIN_VERIFY_REQ` 按 `name`（来自 `Msg_C2S_LoginReq.account`）查找；**password 当前未校验**。

### 1.3 Relation

**读写**：SessionServer 内存 `SessionUser` ↔ RecordServer `RelationStore`（`REC_RELATION_PRELOAD/LOAD/SAVE`）

| 字段 | 说明 |
|------|------|
| `user_id` | 主键 |
| `friends_json` | 好友 ID 列表（逗号分隔文本） |
| `blacklist_json` | 黑名单 ID 列表 |
| `guild_id`, `team_id` | 公会/队伍 |
| `` `binary` `` | 社交扩展二进制（申请列表、缓存等） |

Session 启动时会 **阻塞** 直到 Relation 全表预载完成。

### 1.4 Friend / Mail / MapInfo

表结构已在 `init.sql` 中定义并注释，**当前 C++ 代码无读写**。后续实现时：

- `Friend`：可替代或补充 `Relation.friends_json`
- `Mail`：离线邮件系统
- `MapInfo`：副本/场景独立进度

### 1.5 ServerList

**读写**：SuperServer 启动 `loadServerList()` 只读 MySQL；子服经 `S2S_SERVERLIST_REQ` 从 Super 拉缓存

| server_type | 含义 |
|-------------|------|
| 0 | SuperServer |
| 1 | SessionServer |
| 2 | RecordServer |
| 3 | AOIServer |
| 4 | SceneServer |
| 5 | GatewayServer |

**不含** Logger/Global/Zone/Login（外联服见 `loginserverlist.xml`）。

`init.sql` 种子写入 6 行默认拓扑（9000–9005）。

### 1.6 ZoneInfo

**读写**：LoginServer `ZoneInfoStore`（可选 MySQL；60s 周期 reload）

| 字段 | 说明 |
|------|------|
| `zone_id` | 区号（同 `game_type` 下唯一） |
| `game_type` | 游戏产品类型（0=当前 RPG） |
| `name`, `ip`, `super_port` | 区服展示名与 Super 入口 |
| `enabled` | 1=可登录，0=维护 |

---

## 2. 策划 Lua 配表

### 2.1 生成管线

```
DataDoc/*.xlsx  →  ./gen_data.sh  →  tools/gen_datadoc.py  →  database/<name>_config.lua
```

| 步骤 | 说明 |
|------|------|
| 编辑 | `DataDoc/*.xlsx`（首次 `./gen_data.sh --init` 生成示例） |
| 生成 | `./gen_data.sh` → `database/*_config.lua`（**勿手改** AUTO-GENERATED 文件） |
| 加载 | SceneServer Lua：`DataTable.load("npc_config")` 等 |

Excel 格式：首 sheet、第 1 行字段名、必须有 `id` 列；`a_b_c` → 嵌套 `a.b.c`。

### 2.2 当前配表

| 模块 | 文件 | 消费者 |
|------|------|--------|
| NPC | `database/npc_config.lua` | `script/scene/npc_mgr.lua` |
| 任务 | `database/quest_config.lua` | `script/quest/quest_mgr.lua` |

### 2.3 加载 API

[`basefile/data_table.lua`](../basefile/data_table.lua)：

```lua
local tbl = DataTable.load("npc_config")
local row = DataTable.getById(tbl, 1)
local list = DataTable.filter(tbl, "mapId", 1001)
DataTable.clearCache()  -- 热更前清缓存
```

详见 [basefile/README.md](../basefile/README.md)。

### 2.4 非 DataDoc 配置

| 内容 | 位置 | 说明 |
|------|------|------|
| 技能 | `script/scene/skill_mgr.lua` | **硬编码** `SKILL_CONFIG`，未走 Excel |
| 地图列表 | `config/server_info.xml` | SceneServer C++ 读取 |
| 运行时端口/DB | `config/config.xml` | 各进程 `ConfigLoader` |
| 区内拓扑 | MySQL `ServerList` | 优先于 config 中的端口 |
| 外联地址 | `loginserverlist.xml` | Super `ExternalServerHub` |

---

## 3. 数据边界

```mermaid
flowchart LR
    subgraph persist [MySQL 持久化]
        CB[CharBase]
        REL[Relation]
        SL[ServerList]
        ZI[ZoneInfo]
    end
    subgraph static [Lua 策划表]
        NPC[npc_config]
        QST[quest_config]
    end
    REC[RecordServer] --> CB
    REC --> REL
    SS[SuperServer] --> SL
    LS[LoginServer] --> ZI
    SceneLua[SceneServer Lua] --> NPC
    SceneLua --> QST
```

**原则**：

- 玩家存档变更只经 **RecordServer**
- 静态数值走 **DataDoc → database/**，不在 C++/Lua 硬编码大表
- 集群拓扑：**ServerList**（区内）+ **loginserverlist.xml**（外联）

---

## 4. 初始化命令

```bash
./tables/setup_database.sh                    # 推荐：建库建表
mysql -u root -p < tables/seed_test_data.sql  # 可选：test001/123456
./gen_data.sh                                 # 策划 Lua
```

测试账号见 [README.md](../README.md)。
