#include "GlobalServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <csignal>

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerConfig cfg;
    const char* cfgPath = (argc > 1) ? argv[1] : "../config/config.xml";
    ConfigLoader::Load(cfgPath, cfg);
    Logger::Instance().SetPath(cfg.logPaths.count("GlobalServer")
                                ? cfg.logPaths.at("GlobalServer") : "logs/global.log");
    GlobalServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.globalPort)) return 1;
    server.Run();
    return 0;
}
