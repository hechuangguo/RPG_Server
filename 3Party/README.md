# 3Party 第三方依赖

本目录提供 **Lua 5.4**、**tinyxml2**、**MariaDB Connector/C** 静态库，无需系统安装 `libmysqlclient-dev` 等包。

**源码 tar.gz 已纳入 Git**（`vendor/`），clone 后 `./autoinit.sh` **无需联网下载**，仅从 vendor 解压并编译。

## 目录结构

```
3Party/
├── versions.env              # 版本号与下载 URL（维护者用）
├── download_and_build.sh     # 离线编译（默认）
├── fetch_vendor.sh           # 维护者：从网络更新 vendor/
├── vendor/                   # 源码 tar.gz（纳入 Git）
│   ├── lua-5.4.7.tar.gz
│   ├── tinyxml2-10.0.0.tar.gz
│   └── mariadb-connector-c-3.3.10-src.tar.gz
├── src/                      # 解压临时目录（gitignore）
├── lua/                      # 编译产物 liblua.a（gitignore）
├── tinyxml2/
└── mysql/
```

## Clone 后快速构建

```bash
./autoinit.sh    # 从 vendor 编译 3Party + cmake configure
./Build.sh       # 编译全部服务器
```

- 首次 autoinit 会编译三个静态库（约 1–2 分钟）
- 第二次 autoinit：`.a` 已存在则跳过 3Party 编译
- **不需要 curl**（vendor 已在仓库内）

## 构建依赖

**编译必需**（CentOS/RHEL）：

```bash
sudo dnf install -y gcc-c++ cmake make tar openssl-devel zlib-devel
```

| 工具 | 用途 |
|------|------|
| gcc/g++/make/cmake/tar | 编译静态库 |
| openssl-devel / zlib-devel | MariaDB Connector |
| curl | **仅维护者**运行 `fetch_vendor.sh` 时需要 |

## 脚本用法

| 命令 | 说明 |
|------|------|
| `./3Party/download_and_build.sh` | 默认：离线编译（vendor 齐全） |
| `./3Party/download_and_build.sh --build-only` | 同上 |
| `./3Party/download_and_build.sh --force` | 重新下载 vendor + 重新编译 |
| `./3Party/fetch_vendor.sh` | 仅下载/更新 vendor tar.gz |

## 维护者：升级 3Party 版本

1. 修改 [`versions.env`](versions.env) 中的版本号与 URL
2. `./3Party/fetch_vendor.sh --force`（需 curl 与网络）
3. `git add 3Party/vendor/` 并提交
4. 团队 `git pull` 后执行 `./3Party/download_and_build.sh --force`

详见 [`vendor/README.md`](vendor/README.md)。

## 与项目集成

`CMakeLists.txt` 优先使用 `3Party/lua`、`3Party/tinyxml2`、`3Party/mysql` 下的头文件与 `.a`。

RecordServer 使用 `mysql/mysql.h` + `libmariadbclient.a`，与 `libmysqlclient` API 兼容。

## 版本

| 组件 | 版本 |
|------|------|
| Lua | 5.4.7 |
| tinyxml2 | 10.0.0 |
| MariaDB Connector/C | 3.3.10 |

## 附录：vendor 缺失或下载失败

若 `3Party/vendor/` 缺少 tar.gz（不完整 clone）：

```bash
./3Party/fetch_vendor.sh   # 需网络
```

或手动将对应文件放入 `3Party/vendor/`（文件名见 `vendor/README.md`）。
