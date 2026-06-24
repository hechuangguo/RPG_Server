# database — Lua 策划配表

本目录存放由 **Common/DataDoc** Excel 生成的 **Lua 静态配表**，供 SceneServer 等业务加载。

MySQL 表结构脚本见同级目录 [`tables/`](../tables/)（入口 `tables/init.sql`）。

| 类型 | 文件 | 用途 |
|------|------|------|
| **Lua 配表** | `*_config.lua` | 静态策划数据（SceneServer C++/Lua 加载） |

## Lua 配表

- **来源**：由 [`Common/DataDoc/`](../Common/DataDoc/) 下 Excel 经 `./gen_data.sh` 自动生成
- **勿手改**：文件头含 `AUTO-GENERATED`，修改请编辑 Excel 后重新生成
- **加载**：`basefile/data_table.lua` 中 `DataTable.load("npc_config")` 等；地图表由 C++ `MapConfigLoader` 读取 `map_config`

```bash
./gen_data.sh          # 全量生成
./gen_data.sh --init   # 仅创建示例 Excel
```
