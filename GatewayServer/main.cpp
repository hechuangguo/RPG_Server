#include "GatewayServer.h"
#include "../sdk/util/ConfigLoader.h"

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerConfig cfg;
    const char* cfgPath = (argc > 1) ? argv[1] : "../config/config.xml";
    ConfigLoader::Load(cfgPath, cfg);
    Logger::Instance().SetPath(cfg.logPaths.count("GatewayServer")
                                ? cfg.logPaths.at("GatewayServer") : "logs/gateway.log");
    GatewayServer server;
    // 客户端端口 9005，内部端口 19005
    if (!server.Init(9005, 19005, cfg)) return 1;
    server.Run();
    return 0;
}
