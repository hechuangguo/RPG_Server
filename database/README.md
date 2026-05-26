# database — 数据目录

本目录包含两类数据：

| 类型 | 文件 | 用途 |
|------|------|------|
| **MySQL 结构** | `init.sql` | 玩家存档、账号等持久化（RecordServer） |
| **Lua 配表** | `*_config.lua` | 静态策划数据（SceneServer Lua 加载） |

## Lua 配表

- **来源**：由 [`DataDoc/`](../DataDoc/) 下 Excel 经 `./gen_data.sh` 自动生成
- **勿手改**：文件头含 `AUTO-GENERATED`，修改请编辑 Excel 后重新生成
- **加载**：`basefile/data_table.lua` 中 `DataTable.load("npc_config")` 等

```bash
./gen_data.sh          # 全量生成
./gen_data.sh --init   # 仅创建示例 Excel
```
