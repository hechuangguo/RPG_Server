#include "LoggerServer.h"
#include "../sdk/util/ConfigLoader.h"

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerConfig cfg;
    const char* cfgPath = (argc > 1) ? argv[1] : "../config/config.xml";
    ConfigLoader::Load(cfgPath, cfg);
    std::string logDir = cfg.logPaths.count("LoggerServer")
                         ? cfg.logPaths.at("LoggerServer") : "logs";
    LoggerServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.loggerPort,
                     cfg.superIP, (uint16_t)cfg.superPort,
                     "127.0.0.1", (uint16_t)cfg.sessionPort,
                     logDir)) return 1;
    server.Run();
    return 0;
}
