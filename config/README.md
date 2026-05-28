# config — 运行时 XML 配置

本目录存放各服务器进程启动时读取的 XML 配置。**每个文件须有文件头与关键段注释**（见 [`docs/COMMENTS.md`](../docs/COMMENTS.md)）。

| 文件 | 读取方 | 说明 |
|------|--------|------|
| `config.xml` | 全部服务器（`ConfigLoader` / `ServerBootstrap`） | 数据库、Super 地址、各服端口、日志路径 |
| `server_info.xml` | SceneServer（`SceneInfoLoader`） | 本进程 sceneID 与承载地图列表 |

修改 `config.xml` 后需重启相关进程；`server_info.xml` 仅影响对应 SceneServer 实例。

命令行可覆盖路径：`./SceneServer/SceneServer config/config.xml config/server_info.xml`
