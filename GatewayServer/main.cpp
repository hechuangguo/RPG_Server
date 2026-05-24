#include "GatewayServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <csignal>

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerConfig cfg;
    const char* cfgPath = (argc > 1) ? argv[1] : "../config/config.xml";
    ConfigLoader::Load(cfgPath, cfg);
    Logger::Instance().SetPath(cfg.logPaths.count("GatewayServer")
                                ? cfg.logPaths.at("GatewayServer") : "logs/gateway.log");
    GatewayServer server;
    uint16_t clientPort = (uint16_t)cfg.gatewayPort;
    uint16_t innerPort  = (uint16_t)(cfg.gatewayPort + 10000);
    if (!server.Init(clientPort, innerPort, cfg)) return 1;
    server.Run();
    return 0;
}
