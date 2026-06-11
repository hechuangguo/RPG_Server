# config — 运行时 XML 配置

本目录存放各服务器进程启动时读取的 XML 配置。**每个文件须有文件头与关键段注释**（见 [`docs/COMMENTS.md`](../docs/COMMENTS.md)）。

| 文件 | 读取方 | 说明 |
|------|--------|------|
| `config.xml` | 区内服务器（`ConfigLoader`） | 数据库、Super 地址、LogPaths |
| `server_info.xml` | SceneServer（`SceneInfoLoader`） | 本进程 sceneID 与承载地图列表 |
| `LoggerServer/extern_logger.xml` 等（各服目录） | 对应外联服进程 | 监听 ip/port；Global/Zone 含 LogPath、Database；Global 含 Http |
| `../loginserverlist.xml`（项目根） | 区内各进程（`LoginServerListLoader`） | 外联 Logger/Global/Zone 出站 ip/port/reconnect |

区内 Session/Record/AOI/Scene/Gateway 端口来自 DB 表 `ServerList`（`tables/init.sql`），由 SuperServer 下发。

修改 `config.xml` 或 `loginserverlist.xml` 后需重启相关进程；`server_info.xml` 仅影响对应 SceneServer 实例。

命令行可覆盖路径：`./SceneServer/SceneServer config/config.xml config/server_info.xml`
