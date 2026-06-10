---
name: sdk random and vec
overview: 在 sdk 新增 math 目录，添加随机数静态工具类 Random（thread_local mt19937）与坐标类 Vec1/Vec2/Vec3（float），声明在 .h、实现在 .cpp 并加 Doxygen 注释；同时修改 CMake 让各服务器编译 sdk 的 .cpp。
todos:
  - id: cmake-sdk-src
    content: 修改 CMakeLists.txt add_server 宏，加入 sdk 的 .cpp 源（GLOB_RECURSE SDK_SRC）
    status: completed
  - id: random-class
    content: 新增 sdk/math/Random.h + Random.cpp（静态工具类，thread_local mt19937，常用随机接口 + 模板 shuffle/pick）
    status: completed
  - id: vec-class
    content: 新增 sdk/math/Vec.h + Vec.cpp（Vec1/Vec2/Vec3 float 坐标类与运算接口）
    status: completed
  - id: build-verify
    content: ./Build.sh clean + 编译验证链接通过，读 lints 修复告警
    status: in_progress
isProject: false
---

# sdk 新增 Random 与 Vec 坐标类

## 背景与关键约束

- 现有 `sdk/` 全部为头文件（无 .cpp）。`CMakeLists.txt` 的 `add_server` 仅 `file(GLOB ${SERVER_NAME}/*.cpp)`，**不会编译 sdk 的 .cpp**。
- 用户要求“声明在 .h、实现在 .cpp”，因此**必须改 CMake**把 sdk 的 .cpp 纳入编译，否则实现不参与链接。
- 仓库无既有 Vector/Random/Coord 类型，无重名冲突。
- 决策（已确认）：坐标=三个独立类 `Vec1/Vec2/Vec3`（float）；随机=静态工具类（thread_local `std::mt19937`）。

## 1) 随机数类 — [`sdk/math/Random.h`](/home/hcg/RPG/sdk/math/Random.h) + `Random.cpp`

静态工具类（无状态对外，内部 `thread_local std::mt19937` 引擎），风格对齐 [`sdk/time/TimeUtil.h`](/home/hcg/RPG/sdk/time/TimeUtil.h)。

- .cpp 实现（非模板）：
  - `static void seed(uint32_t s)` / 默认用 `random_device` 自动播种
  - `static int range(int minV, int maxV)`（闭区间 `[minV,maxV]`）
  - `static int64_t range64(int64_t minV, int64_t maxV)`
  - `static uint32_t rangeU(uint32_t minV, uint32_t maxV)`
  - `static double rangeF(double minV, double maxV)`
  - `static double next01()`（`[0,1)`）
  - `static bool chance(double prob)`（`prob∈[0,1]`）
  - `static bool percent(int p)`（`p∈[0,100]`）
  - `static bool boolean()`
- .h 内联模板助手（模板必须在头实现）：
  - `template<class It> static void shuffle(It first, It last)`
  - `template<class T> static const T& pick(const std::vector<T>& v)`（空容器 UB，注释提醒）
- 私有：`static std::mt19937& engine()`（在 .cpp 定义 `thread_local` 实例）。

## 2) 坐标类 — [`sdk/math/Vec.h`](/home/hcg/RPG/sdk/math/Vec.h) + `Vec.cpp`

三个类，分量类型 `float`（与 `CharBase.pos_x/y/z` 一致）。声明在 .h，运算实现在 .cpp。

- `Vec1{ float x; }`：构造、`operator+ - == !=`、标量 `*`、`length()`、`distanceTo()`、`isZero()`。
- `Vec2{ float x,y; }`：上述 + `dot()`、`lengthSq()`、`distanceSqTo()`、`normalize()`/`normalized()`、`+= -=`、`toString()`。
- `Vec3{ float x,y,z; }`：同 `Vec2` + `cross()`。
- 浮点比较用带 `EPSILON`（`constexpr float`）的近似相等，避免裸 `==` 误差；`normalize` 对零向量保护。

## 3) 构建集成 — [`CMakeLists.txt`](/home/hcg/RPG/CMakeLists.txt)

在 `add_server` 宏内追加 sdk 源（约 142-146 行）：

```cmake
file(GLOB_RECURSE SDK_SRC "${CMAKE_SOURCE_DIR}/sdk/*.cpp")
add_executable(${SERVER_NAME} ${SERVER_SRC} ${SDK_SRC})
```

使每个服务器都编译/链接 `sdk/math/*.cpp`（按需被链接，未引用符号不影响体积）。

## 4) 注释与规范

- 三文件均含文件头 `@file`/`@brief`；类/方法 `@brief`，非显然参数 `@param`/`@return`；成员 `/**< */`。
- 命名：类 PascalCase（`Random`/`Vec1/2/3`），方法 camelCase（`range`/`distanceTo`/`normalize`），常量 ALL_CAPS（`EPSILON`）。

## 5) 验证

- `./Build.sh clean && ./Build.sh SuperServer`（clean 让 CMake 重新 glob 到新 sdk .cpp），确认编译链接通过。
- 读取新文件 lints，修复本次引入告警（既有 clangd 路径告警不处理）。

## 验收标准

- `sdk/math/` 下 `Random.h/.cpp`、`Vec.h/.cpp` 四文件齐全，声明/实现分离且有注释。
- `Random` 提供常用随机接口；`Vec1/2/3` 提供常用坐标运算。
- CMake 编译 sdk .cpp，至少一个服务器编译链接通过。