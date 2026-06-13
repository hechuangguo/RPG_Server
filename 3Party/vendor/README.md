# vendor — 第三方源码包（纳入 Git）

本目录存放 **Lua / tinyxml2 / MariaDB Connector** 的官方源码 tar.gz，clone 后 `./autoinit.sh` **无需联网下载**，仅从本目录解压并编译为静态库。

## 文件清单

| 文件 | 版本（见 `../versions.env`） |
|------|------------------------------|
| `lua-5.4.7.tar.gz` | Lua 5.4.7 |
| `tinyxml2-10.0.0.tar.gz` | tinyxml2 10.0.0 |
| `mariadb-connector-c-3.3.10-src.tar.gz` | MariaDB Connector/C 3.3.10 |

**禁止手改** tar.gz 内容；升级版本请维护者运行 [`../fetch_vendor.sh`](../fetch_vendor.sh) 后 commit 新包。

## 维护者升级流程

1. 修改 [`../versions.env`](../versions.env) 中的版本号与 URL
2. `./3Party/fetch_vendor.sh`（需 curl，下载到本目录）
3. `git add 3Party/vendor/` 并提交
4. 团队 `git pull` 后执行 `./3Party/download_and_build.sh --force` 重新编译

详见 [`../README.md`](../README.md)。
