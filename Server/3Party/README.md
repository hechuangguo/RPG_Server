# 3Party 第三方依赖

本目录存放项目所需的 **Lua 5.4**、**tinyxml2**、**MySQL 客户端（MariaDB Connector/C）** 静态库，无需系统安装 `libmysqlclient-dev` / `lua5.4-dev` / `libtinyxml2-dev`。

## 目录结构（构建后）

```
3Party/
├── versions.env              # 版本与下载 URL
├── download_and_build.sh     # 一键下载并编译
├── cache/                    # 源码压缩包缓存（可删后重下）
├── src/                      # 解压后的源码（可删）
├── lua/
│   ├── include/              # lua.h, lualib.h, ...
│   └── lib/liblua.a
├── tinyxml2/
│   ├── include/tinyxml2.h
│   └── lib/libtinyxml2.a
└── mysql/
    ├── include/mysql/mysql.h # MariaDB Connector，兼容 libmysqlclient API
    └── lib/libmariadb.a
```

## 构建依赖

**仅需构建工具**（CentOS/RHEL）：

```bash
sudo dnf install -y gcc-c++ cmake make curl tar openssl-devel zlib-devel
```

## 一键构建

```bash
chmod +x 3Party/download_and_build.sh
./3Party/download_and_build.sh
```

强制重新下载编译：

```bash
./3Party/download_and_build.sh --force
```

## 与项目集成

`CMakeLists.txt` 会 **优先** 使用 `3Party/` 下的头文件与静态库；`autoinit.sh` 在配置前会自动调用本脚本（若库尚未构建）。

```bash
./autoinit.sh    # 构建 3Party（如需要）+ cmake configure
./build.sh       # 编译全部服务器
```

## 版本

| 组件 | 版本 | 说明 |
|------|------|------|
| Lua | 5.4.7 | 官方源码静态库 |
| tinyxml2 | 10.0.0 | GitHub release |
| MariaDB Connector/C | 3.3.10 | 提供 `mysql/mysql.h`，链接 `libmariadb.a` |

RecordServer 使用 `mysql/mysql.h` + `libmariadb.a`，与 `libmysqlclient` API 兼容。
