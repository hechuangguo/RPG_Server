---
name: SceneServer 管理器独立化计划
overview: 按你的最新约束收敛：各管理器独立实现 init/loop/needSave/save/load/add/remove；SceneUser 仅持有管理器成员属性，不做按类型分发与调度编排。
todos:
  - id: create-manager-files
    content: 在 SceneServer 同级新增 Bag/EquipBag/StoreBag/BagManager 与 Item/Spell/Buff/Task 管理器文件并定义独立接口
    status: completed
  - id: implement-bag-methods
    content: 实现 Bag 与 BagManager 的初始化、取包、增删、合并、拆分、遍历基础逻辑
    status: completed
  - id: implement-other-managers
    content: 实现 ItemManager/SpellManager/BuffManager/TaskManager 的 init/loop/needSave/save/load/add/remove 简单方法
    status: completed
  - id: mount-on-sceneuser
    content: 将各管理器作为 SceneUser 成员属性，保持 SceneUser 不做类型分发
    status: completed
  - id: build-check
    content: 执行 SceneServer 编译验证新增类接入
    status: completed
isProject: false
---

# SceneServer 管理器独立化计划

## 目标

- 新增并实现以下类（与 [`/home/hcg/RPG/SceneServer/SceneUser.h`](/home/hcg/RPG/SceneServer/SceneUser.h) 同级目录）：
  - `Bag`, `EquipBag`, `StoreBag`, `BagManager`
  - `ItemManager`, `SpellManager`, `BuffManager`, `TaskManager`
- 每个管理器类都具备自己的 `init/loop/needSave/save/load/add/remove` 方法。
- `SceneUser` 仅作为“持有者”，把这些管理器作为成员属性挂载，不负责管理器逻辑分发。

## 设计边界

- `SceneUser` 不新增 `saveByType/loadByType/needSaveByType` 一类按类型分发函数。
- 不扩展 RecordServer、不新增 SQL 持久化链路（保持当前范围）。
- 管理器接口与内部状态自洽，互不依赖 SceneUser 生命周期编排。

## 代码改动点

1. 在 `SceneServer/` 同级新增以下文件：
   - `Bag.h/.cpp`, `EquipBag.h/.cpp`, `StoreBag.h/.cpp`, `BagManager.h/.cpp`
   - `ItemManager.h/.cpp`, `SpellManager.h/.cpp`, `BuffManager.h/.cpp`, `TaskManager.h/.cpp`
2. 修改 [`/home/hcg/RPG/SceneServer/SceneUser.h`](/home/hcg/RPG/SceneServer/SceneUser.h)：
   - 增加成员属性：`BagManager bagManager; ItemManager itemManager; SpellManager spellManager; BuffManager buffManager; TaskManager taskManager;`
   - 不加入分发逻辑方法。
3. 修改 [`/home/hcg/RPG/SceneServer/SceneUser.cpp`](/home/hcg/RPG/SceneServer/SceneUser.cpp)：
   - 仅在适当时机调用最小必要方法（例如 `init` 时调用各 `manager.init()`），不实现类型路由系统。
4. 检查 [`/home/hcg/RPG/CMakeLists.txt`](/home/hcg/RPG/CMakeLists.txt) 对新增 `.cpp` 的收集是否覆盖。

## Bag 与 BagManager 具体能力

- `Bag`：初始化、槽位校验、按槽位取道具、遍历槽位。
- `EquipBag`/`StoreBag`：继承 `Bag` 并定义各自容量/类型。
- `BagManager`：
  - 按包裹类型获取实例
  - 包裹增删道具
  - 初始化全部包裹
  - 道具合并（同物品堆叠）
  - 道具拆分（从源槽分数量到目标槽）
  - 遍历所有包裹

## 验证

- `./build.sh SceneServer` 编译通过。
- 管理器方法可独立调用，`SceneUser` 仅作为成员容器不承担分发职责。
