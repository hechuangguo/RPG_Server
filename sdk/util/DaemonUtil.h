/**
 * @file    DaemonUtil.h
 * @brief   进程守护化工具（-d 参数剥离与 fork 后台运行）
 *
 * 各服务器 main 在解析配置路径前调用 extractAndDaemonize，
 * 避免将 -d 误当作 config.xml / extern_*.xml 路径。
 */

#pragma once

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace DaemonUtil {

/**
 * @brief 从 argv 移除 -d，并在出现时执行守护化
 * @param argc [in/out] 参数个数（移除 -d 后递减）
 * @param argv 参数数组（压缩后保证 argv[argc]==nullptr）
 * @return 若本次调用触发了守护化（父进程已 exit）则不会返回；子进程返回 true
 */
inline bool extractAndDaemonize(int& argc, char** argv)
{
    bool wantDaemon = false;
    int writeIdx    = 1;
    for (int readIdx = 1; readIdx < argc; ++readIdx)
    {
        if (std::strcmp(argv[readIdx], "-d") == 0)
        {
            wantDaemon = true;
            continue;
        }
        argv[writeIdx++] = argv[readIdx];
    }
    argc           = writeIdx;
    argv[argc]     = nullptr;

    if (!wantDaemon)
        return false;

    const pid_t pid = ::fork();
    if (pid < 0)
        std::_Exit(1);
    if (pid > 0)
        std::_Exit(0);

    if (::setsid() < 0)
        std::_Exit(1);

    const int nullFd = ::open("/dev/null", O_RDWR);
    if (nullFd >= 0)
    {
        ::dup2(nullFd, STDIN_FILENO);
        ::dup2(nullFd, STDOUT_FILENO);
        ::dup2(nullFd, STDERR_FILENO);
        if (nullFd > STDERR_FILENO)
            ::close(nullFd);
    }
    return true;
}

}  // namespace DaemonUtil
