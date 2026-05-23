#include "RecordServer.h"
#include "../sdk/util/ConfigLoader.h"

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerConfig cfg;
    const char* cfgPath = (argc > 1) ? argv[1] : "../config/config.xml";
    ConfigLoader::Load(cfgPath, cfg);
    Logger::Instance().SetPath(cfg.logPaths.count("RecordServer")
                                ? cfg.logPaths.at("RecordServer") : "logs/record.log");
    RecordServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.recordPort, cfg)) return 1;
    server.Run();
    return 0;
}
