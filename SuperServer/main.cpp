#include "SuperServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <cstdio>
#include <csignal>

static bool g_running = true;
void SignalHandler(int) { g_running = false; }

int main(int argc, char* argv[])
{
    signal(SIGINT,  SignalHandler);
    signal(SIGTERM, SignalHandler);
    signal(SIGPIPE, SIG_IGN);

    ServerConfig cfg;
    const char* cfgPath = (argc > 1) ? argv[1] : "../config/config.xml";
    if (!ConfigLoader::Load(cfgPath, cfg))
    {
        fprintf(stderr, "Failed to load config: %s\n", cfgPath);
        return 1;
    }

    Logger::Instance().SetPath(cfg.logPaths.count("SuperServer")
                               ? cfg.logPaths.at("SuperServer") : "logs/super.log");

    SuperServer server;
    if (!server.Init(cfg.superIP, (uint16_t)cfg.superPort)) return 1;
    server.Run();
    return 0;
}
