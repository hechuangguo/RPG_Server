# 地图运行时数据（maps/）

Unity MapExporter 导出或服务端维护的 **3D 地图 runtime** 数据，供 SceneServer `MapDataLoader` 加载。

## 目录结构

```
maps/runtime/{mapId}/
├── map.meta.json       # 必填：bounds、aoiGridSize、version
├── spawns.json         # 出生/复活点
├── npc_placements.json # 可选
├── teleports.json      # 可选
├── triggers.json       # 可选
└── navmesh.bin         # 可选（Recast，Phase 2+）
```

## mapId 分段

与 [`config/server_info.xml`](../config/server_info.xml) 一致：

| 段 | 类型 |
|----|------|
| 1000–1999 | 主城/新手村 |
| 2000–2999 | 野外 |
| 3000–3999 | 副本 |
| 4000–4999 | PvP |

## 校验

```bash
./tools/map_export/validate_map.sh maps/runtime/1001
```

完整 schema 与 Unity 导出流程见 [docs/3D_DESIGN.md](../docs/3D_DESIGN.md)。
