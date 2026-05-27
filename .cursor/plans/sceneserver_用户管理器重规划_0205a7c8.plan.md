---
name: SceneServer 用户管理器重规划
overview: 按你最新要求改为“每个 SceneUser 持有独立管理器成员”，并将所有新增类放在 `SceneServer/` 与 `SceneUser.h` 同级目录；SceneUser 基于功能类型枚举统一分发各管理器的 init/loop/needSave/save/load/add/remove。
todos:
  - id: feature-type-and-layout
    content: 在 SceneServer 同级新增 UserFeatureType 与各管理器/包裹类骨架（非单例）
    status: pending
  - id: bag-system
    content: 实现 Bag/EquipBag/StoreBag 基础能力与 BagManager 的取包、增删、合并、拆分、遍历
    status: pending
  - id: other-managers
    content: 实现 ItemManager/SpellManager/BuffManager/TaskManager 的 init/loop/needSave/save/load/add/remove
    status: pending
  - id: sceneuser-integration
    content: 将管理器作为 SceneUser 成员属性并实现按 UserFeatureType 分发的 save/load/needSave/init/loop
    status: pending
  - id: build-verify
    content: 检查 CMake 收集规则并执行 ./build.sh SceneServer 验证
    status: pending
isProject: false
---

# SceneServer 用户管理器重规划

## 实现范围

- 在 [`/home/hcg/RPG/SceneServer/SceneUser.h`](/home/hcg/RPG/SceneServer/SceneUser.h) 同级目录新增：
  - `Bag.h/.cpp`
  - `EquipBag.h/.cpp`
  - `StoreBag.h/.cpp`
  - `BagManager.h/.cpp`
  - `ItemManager.h/.cpp`
  - `SpellManager.h/.cpp`
  - `BuffManager.h/.cpp`
  - `TaskManager.h/.cpp`
  - `UserFeatureType.h`（功能/存档类型枚举）
- 修改 [`/home/hcg/RPG/SceneServer/SceneUser.h`](/home/hcg/RPG/SceneServer/SceneUser.h)、[`/home/hcg/RPG/SceneServer/SceneUser.cpp`](/home/hcg/RPG/SceneServer/SceneUser.cpp)、[`/home/hcg/RPG/CMakeLists.txt`](/home/hcg/RPG/CMakeLists.txt)。

## 核心设计

### 1) 管理器实例模型

- 按你确认：**每个 `SceneUser` 持有自己的管理器成员对象（非单例）**。
- `SceneUser` 成员示例：`BagManager bagManager; ItemManager itemManager; SpellManager spellManager; BuffManager buffManager; TaskManager taskManager;`

### 2) 功能类型枚举驱动

- 新增 `enum class UserFeatureType`（如：`CHAR_BASE / BAG / ITEM / SKILL / BUFF / TASK`）。
- `SceneUser` 提供并实现：
  - `bool needSaveByType(UserFeatureType type) const;`
  - `bool saveByType(UserFeatureType type);`
  - `bool loadByType(UserFeatureType type);`
  - `bool initByType(UserFeatureType type);`
  - `void loopByType(UserFeatureType type, uint64_t nowMs);`
- `SceneUser::needSave/save/load/init/loop` 内统一遍历类型并分发到对应管理器。

### 3) 管理器统一接口（简单实现）

每个管理器实现以下基础方法：

- `init()`
- `loop(uint64_t nowMs)`
- `needSave() const`
- `save()`
- `load()`
- `add(...)`
- `remove(...)`

> 这些接口先做轻量可运行版本（内存结构 + 简单序列化字符串/容器），不扩展 RecordServer/SQL。

### 4) Bag 与 BagManager 细化

- `Bag`（基类）实现：
  - 初始化格子
  - `getSlotIndex(...)` / 校验槽位
  - `getItemBySlot(slot)`
  - `forEachSlot(fn)` 遍历
- `EquipBag` / `StoreBag` 继承 `Bag`，分别定义槽位容量与类型标识。
- `BagManager` 持有多个包裹实例（至少 equip/store），实现：
  - `Bag* getBagByType(BagType type)`
  - `addItem(type, slot, itemId, count)`
  - `removeItem(type, slot, count)`
  - `init()`
  - `mergeItem(...)`（合并堆叠）
  - `splitItem(...)`（拆分堆叠）
  - `forEachBag(fn)`

## 变更步骤

1. 新增 `UserFeatureType.h` 与所有管理器/包裹类（同级目录）。
2. 完成 `Bag`、`EquipBag`、`StoreBag`、`BagManager` 基础方法与数据结构。
3. 完成 `ItemManager`、`SpellManager`、`BuffManager`、`TaskManager` 的基础生命周期与增删接口。
4. 改造 `SceneUser`：添加管理器成员属性、按类型分发函数、聚合 `needSave/save/load/loop`。
5. 调整 `CMakeLists.txt`（确保 SceneServer 同级新增 `.cpp` 编译进目标；若已用递归扫描则仅校验无误）。
6. 编译验证：`./build.sh SceneServer`。

## 验证点

- 编译通过且 `SceneUser` 生命周期完整调用各管理器。
- `needSave()` 能正确聚合各类型结果。
- `saveByType/loadByType` 对不同类型分发正确。
- `BagManager` 的取包、增删、合并、拆分、遍历方法可正常调用。

## 说明

- 按你最新描述，已不采用“全局单例 manager + userId 分桶”方案。
- 不改动计划文件本身，仅按新需求执行代码实现。
