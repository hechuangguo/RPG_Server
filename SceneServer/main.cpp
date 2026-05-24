#include "SceneServer.h"
#include "../sdk/util/ConfigLoader.h"
#include <csignal>
#include "../sdk/util/SceneInfoLoader.h"

int main(int argc, char* argv[])
{
    signal(SIGPIPE, SIG_IGN);
    ServerConfig cfg;
    const char* cfgPath       = (argc > 1) ? argv[1] : "../config/config.xml";
    const char* sceneInfoPath = (argc > 2) ? argv[2] : "../config/server_info.xml";
    ConfigLoader::Load(cfgPath, cfg);
    SceneServerInfo sceneInfo;
    SceneInfoLoader::Load(sceneInfoPath, sceneInfo);
    Logger::Instance().SetPath(cfg.logPaths.count("SceneServer")
                                ? cfg.logPaths.at("SceneServer") : "logs/scene.log");
    SceneServer server;
    if (!server.Init("0.0.0.0", (uint16_t)cfg.scenePort, cfg, sceneInfo)) return 1;
    server.Run();
    return 0;
}
