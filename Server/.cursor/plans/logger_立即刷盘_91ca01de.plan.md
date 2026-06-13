---
name: Logger 立即刷盘
overview: 修改 [`sdk/log/Logger.h`](sdk/log/Logger.h)，使每条日志写入后立即 `fflush(stdout)` 并刷新文件缓冲区，解决 `logs/super.log` 长期为 0 字节、`tail -F` 看不到启动日志的问题。
todos:
  - id: flush-logger
    content: 在 Logger::Log 中每条日志后 fflush(stdout) + m_writer.Flush()
    status: completed
  - id: update-comment
    content: 更新 Logger.h 头部注释说明即时刷盘行为
    status: completed
  - id: verify-super-log
    content: 编译并验证 tail -F logs/super.log 能立即看到启动日志
    status: completed
isProject: false
---

# Logger 每条日志立即落盘

## 问题根因

[`Logger::Log`](sdk/log/Logger.h) 当前逻辑：

```103:108:sdk/log/Logger.h
        std::lock_guard<std::mutex> lk(m_mu);
        fwrite(line, 1, n, stdout);
        if (m_writer.HasPath())
            m_writer.Write(line, static_cast<size_t>(n));
        if (lv == LogLevel::FATAL)
            m_writer.Flush();
```

- 文件侧：仅 `FATAL` 调用 `m_writer.Flush()`，普通 `INFO` 留在 stdio 全缓冲（约 4KB）中。
- 标准输出：`fwrite` 到 stdout 后无 `fflush`；经 `RunServer.sh` 的 `nohup` 重定向到文件时也是全缓冲，故 `logs/SuperServer_stdout.log` 同样长期为空。
- 进程被 `kill -9` 时缓冲区不会落盘，表现为「启动成功但没有任何日志」。

[`LogFileWriter`](sdk/log/LogFileWriter.h) 已提供 `Flush()`，[`LoggerServer`](LoggerServer/LoggerServer.h) 远程日志写入后也会主动 `Flush()`，行为一致。

## 修改方案

**仅改一个文件：** [`sdk/log/Logger.h`](sdk/log/Logger.h)

在 `Log()` 写入 `stdout` 与 `m_writer` 之后，统一刷盘：

```cpp
        fwrite(line, 1, n, stdout);
        fflush(stdout);
        if (m_writer.HasPath()) {
            m_writer.Write(line, static_cast<size_t>(n));
            m_writer.Flush();
        }
```

并更新文件头注释：说明每条日志写入后即时刷盘（不再仅 FATAL 刷盘）。

## 影响范围

- 所有使用 `LOG_*` 宏的服务器（SuperServer、SessionServer、SceneServer 等）均受益。
- 开发环境 `tail -F logs/super.log` / `./log.sh` 可立即看到启动行。
- 代价：每条日志多一次 `fflush`，对当前规模可接受；若日后需优化，可加 `SetAutoFlush(bool)` 开关（本次不做，保持最小改动）。

## 验证步骤

1. 重新编译：`./build.sh`（或项目现有构建命令）。
2. 停旧进程：`./StopServer.sh`。
3. 前台或脚本启动 SuperServer：
   ```bash
   cd /home/hechuangguo/RPG
   ./.build/bin/SuperServer config/config.xml
   ```
4. 另一终端：
   ```bash
   tail -F logs/super.log
   ```
   应立刻出现 `SuperServer starting` / `SuperServer started`。
5. 确认 `logs/SuperServer_stdout.log`（若用 `RunServer.sh`）同样非空。

## 不改动

- [`LogFileWriter.h`](sdk/log/LogFileWriter.h) 逻辑不变。
- `RunServer.sh` / `log.sh` 路径与用法不变。
- 各服务器 `main.cpp` 无需修改。
