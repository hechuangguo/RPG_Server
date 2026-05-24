#include "ZoneServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <csignal>

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerConfig cfg;
    const char* cfgPath = (argc > 1) ? argv[1] : "../config/config.xml";
    ConfigLoader::Load(cfgPath, cfg);
    Logger::Instance().SetPath(cfg.logPaths.count("ZoneServer")
                                ? cfg.logPaths.at("ZoneServer") : "logs/zone.log");
    ZoneServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.zonePort)) return 1;
    server.Run();
    return 0;
}
