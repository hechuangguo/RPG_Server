# DataDoc — 服务器策划数据文档

本目录存放 **Excel（`.xlsx`）** 策划表，为服务器静态数据的唯一编辑入口。

## 工作流

1. 策划在 Excel 中编辑表（如 `npc.xlsx`、`quest.xlsx`）
2. 在项目根目录执行：`./gen_data.sh`
3. 工具生成 Lua 到 [`../database/`](../database/)（如 `npc_config.lua`）
4. SceneServer 通过 [`../basefile/data_table.lua`](../basefile/data_table.lua) 加载使用

首次使用可生成示例表：

```bash
./gen_data.sh --init
./gen_data.sh
```

## 表格式约定

| 规则 | 说明 |
|------|------|
| 文件 | 每个 `.xlsx` 对应一张逻辑表，输出为 `database/<文件名>_config.lua` |
| 工作表 | 仅读取 **第一个** Sheet |
| 第 1 行 | 字段名（英文，建议 camelCase） |
| 第 2 行起 | 数据；**`id` 列必填**，作为主键 |
| 嵌套字段 | 列名用下划线分层，如 `reward_exp` → `reward.exp` |
| 类型 | 自动识别整数、浮点、布尔、字符串 |

## 示例文件

| 文件 | 说明 |
|------|------|
| `npc.xlsx` | NPC 分布、类型、对话脚本 |
| `quest.xlsx` | 任务配置与奖励 |

## 依赖

- Python 3
- `openpyxl`（`pip3 install -r tools/requirements-datadoc.txt`）

`gen_data.sh` 会在缺少依赖时尝试自动安装。
