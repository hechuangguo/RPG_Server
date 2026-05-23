#include "AOIServer.h"
#include "../sdk/util/ConfigLoader.h"

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerConfig cfg;
    const char* cfgPath = (argc > 1) ? argv[1] : "../config/config.xml";
    ConfigLoader::Load(cfgPath, cfg);
    Logger::Instance().SetPath(cfg.logPaths.count("AOIServer")
                                ? cfg.logPaths.at("AOIServer") : "logs/aoi.log");
    AOIServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.aoiPort,
                     cfg.superIP, (uint16_t)cfg.superPort,
                     "127.0.0.1", (uint16_t)cfg.sessionPort)) return 1;
    server.Run();
    return 0;
}
