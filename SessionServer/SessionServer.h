/**
 * @file    SessionServer.h
 * @brief  会话服务器 —— 社会关系 + 全区场景/副本管理
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/util/WireStringUtil.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include "../common/ClientMsg.h"
#include "../sdk/net/MsgId.h"
#include "SessionUser.h"
#include "SessionUserManager.h"
#include "SessionSceneManager.h"
#include <mysql/mysql.h>
#include <string>

class SessionServer : public INetCallback
{
public:
    SessionServer() : m_server(this), m_superClient(this), m_db(nullptr) {}
    ~SessionServer() { if (m_db) mysql_close(m_db); }

    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg)
    {
        Logger::Instance().SetServerName("SessionServer");
        LOG_INFO("SessionServer starting on %s:%d", ip.c_str(), port);
        if (!m_server.Start(ip, port)) { LOG_FATAL("Start failed"); return false; }
        if (!InitDB(cfg)) { LOG_FATAL("DB init failed"); return false; }

        if (!m_superClient.Connect(cfg.superIP, (uint16_t)cfg.superPort))
            LOG_WARN("Cannot connect to SuperServer");

        RegisterHandlers();

        TimerMgr::Instance().Register(500, 0, [this]{ RegisterToSuper(); });
        TimerMgr::Instance().Register(10000, 10000, [this]{ SendHeartbeat(); });
        TimerMgr::Instance().Register(60000, 60000, [this]{ AutoSaveAll(); });
        LOG_INFO("SessionServer started (MySQL + SceneManager).");
        return true;
    }

    void Run()
    {
        while (true)
        {
            m_superClient.Poll(0);
            m_server.Poll(10);
            TimerMgr::Instance().Update();
        }
    }

    void OnConnect(ConnID id) override { LOG_INFO("InnerConn connected=%u", id); }

    void OnDisconnect(ConnID id) override
    {
        m_sceneManager.unbindConn(id);
        LOG_INFO("InnerConn disconnected=%u", id);
    }

    void OnMessage(ConnID id, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, module, sub, data, len);
    }

private:
    bool InitDB(const ServerConfig& cfg)
    {
        m_db = mysql_init(nullptr);
        if (!m_db) return false;
        if (!mysql_real_connect(m_db,
            cfg.dbHost.c_str(), cfg.dbUser.c_str(),
            cfg.dbPass.c_str(), cfg.dbName.c_str(),
            (unsigned int)cfg.dbPort, nullptr, 0))
        {
            LOG_ERR("MySQL connect failed: %s", mysql_error(m_db));
            return false;
        }
        mysql_set_character_set(m_db, "utf8mb4");
        LOG_INFO("SessionServer MySQL connected: %s:%d/%s",
                 cfg.dbHost.c_str(), cfg.dbPort, cfg.dbName.c_str());
        return true;
    }

    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        d.Register((uint16_t)InternalMsgID::S2S_HEARTBEAT_ACK,
            [](uint32_t, const char*, uint16_t){ });
        d.Register((uint16_t)InternalMsgID::SES_LOAD_USER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnLoadUserReq(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SES_SAVE_USER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnSaveUserReq(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SES_FRIEND_UPDATE,
            [this](uint32_t c, const char* d, uint16_t l){ OnFriendUpdate(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SES_SCENE_REGISTER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnSceneRegisterReq(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SES_SCENE_UNREGISTER,
            [this](uint32_t c, const char* d, uint16_t l){ OnSceneUnregister(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SES_COPY_CREATE_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnCopyCreateReq(c, d, l); });
        d.Register((uint16_t)InternalMsgID::GW_CLIENT_MSG,
            [this](uint32_t c, const char* d, uint16_t l){ OnGatewayClientMsg(c, d, l); });
    }

    /**
     * @brief Gateway 转发的客户端消息（社交/任务等）
     */
    void OnGatewayClientMsg(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_GW_ClientMsg)) return;
        const auto* hdr = reinterpret_cast<const Msg_GW_ClientMsg*>(data);
        const char* body = data + sizeof(Msg_GW_ClientMsg);
        if (len < sizeof(Msg_GW_ClientMsg) + hdr->dataLen) return;

        LOG_INFO("GatewayClientMsg: conn=%u mod=0x%02X sub=0x%02X len=%u",
                 hdr->clientConnID, hdr->module, hdr->sub, hdr->dataLen);

        if (hdr->module == static_cast<uint8_t>(ClientModule::SOCIAL))
            handleSocialClientMsg(hdr->clientConnID, hdr->sub, body, hdr->dataLen);
        else if (hdr->module == static_cast<uint8_t>(ClientModule::QUEST))
            handleQuestClientMsg(hdr->clientConnID, hdr->sub, body, hdr->dataLen);
    }

    void handleSocialClientMsg(uint32_t clientConnId, uint8_t sub,
                               const char* /*data*/, uint16_t len)
    {
        LOG_DEBUG("SocialClientMsg: conn=%u sub=0x%02X len=%u", clientConnId, sub, len);
    }

    void handleQuestClientMsg(uint32_t clientConnId, uint8_t sub,
                              const char* /*data*/, uint16_t len)
    {
        LOG_DEBUG("QuestClientMsg: conn=%u sub=0x%02X len=%u", clientConnId, sub, len);
    }

    void RegisterToSuper()
    {
        Msg_S2S_Register reg{};
        reg.serverType = (uint8_t)SubServerType::SESSION;
        reg.serverID   = 1;
        copyToWire(reg.ip, sizeof(reg.ip), "127.0.0.1");
        reg.port       = 9001;
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                               reinterpret_cast<char*>(&reg), sizeof(reg));
    }

    void SendHeartbeat()
    {
        Msg_S2S_Heartbeat hb{};
        hb.seq       = ++m_hbSeq;
        hb.timestamp = TimerMgr::NowMs();
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                               reinterpret_cast<char*>(&hb), sizeof(hb));
    }

    void OnLoadUserReq(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(UserID)) return;
        UserID uid = *reinterpret_cast<const UserID*>(data);

        auto user = m_userManager.getOrCreateUser(uid);
        user->load(m_db);
        user->onOnline();

        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_LOAD_USER_RSP,
                         data, len);
    }

    void OnSaveUserReq(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(UserID)) return;
        UserID uid = *reinterpret_cast<const UserID*>(data);
        auto user = m_userManager.findUser(uid);
        if (!user)
            user = m_userManager.getOrCreateUser(uid);
        user->save(m_db);
    }

    void OnFriendUpdate(ConnID /*fromConn*/, const char* /*data*/, uint16_t len)
    {
        LOG_DEBUG("FriendUpdate len=%d", len);
    }

    /** @brief SceneServer 注册普通/副本场景 */
    void OnSceneRegisterReq(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_SES_SceneRegisterReq)) return;
        const auto* req = reinterpret_cast<const Msg_SES_SceneRegisterReq*>(data);

        m_sceneManager.registerScene(fromConn, *req);

        Msg_SES_SceneRegisterRsp rsp{};
        rsp.code             = 0;
        rsp.sceneInstanceId  = req->sceneInstanceId;
        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_SCENE_REGISTER_RSP,
                          reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    void OnSceneUnregister(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_SES_SceneUnregister)) return;
        const auto* req = reinterpret_cast<const Msg_SES_SceneUnregister*>(data);
        m_sceneManager.unregisterScene(req->sceneInstanceId, req->sceneServerId);
    }

    /** @brief 副本创建：复用已有或负载均衡分配新副本 */
    void OnCopyCreateReq(ConnID fromConn, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_SES_CopyCreateReq)) return;
        const auto* req = reinterpret_cast<const Msg_SES_CopyCreateReq*>(data);

        m_sceneManager.bindSceneServer(fromConn, req->reqSceneServerId);

        Msg_SES_CopyCreateRsp rsp{};
        rsp.code = 0;

        const CopyType copyType = static_cast<CopyType>(req->copyType);
        SessionCopyScene* existing = m_sceneManager.findReusableCopy(
            copyType, req->mapId, req->ownerId);

        if (existing)
        {
            rsp.targetSceneServerId = existing->getSceneServerId();
            rsp.copyInstanceId      = existing->getCopyInstanceId();
            rsp.copyType            = req->copyType;
            rsp.mapId               = req->mapId;
            rsp.ownerId             = req->ownerId;
            rsp.maxPlayer           = existing->getMaxPlayer();
            rsp.reused              = 1;
            copyWireField(rsp.mapName, req->mapName);
            copyWireField(rsp.mapFile, req->mapFile);
        }
        else
        {
            uint32_t targetId = m_sceneManager.pickSceneServerId();
            if (targetId == 0)
                targetId = req->reqSceneServerId;

            const uint64_t copyId = m_sceneManager.generateCopyInstanceId();
            m_sceneManager.createCopyRecord(targetId, copyId, *req);

            rsp.targetSceneServerId = targetId;
            rsp.copyInstanceId      = copyId;
            rsp.copyType            = req->copyType;
            rsp.mapId               = req->mapId;
            rsp.ownerId             = req->ownerId;
            rsp.maxPlayer           = req->maxPlayer;
            rsp.reused              = 0;
            copyWireField(rsp.mapName, req->mapName);
            copyWireField(rsp.mapFile, req->mapFile);

            Msg_SES_CopyCreateCmd cmd{};
            cmd.copyInstanceId = copyId;
            cmd.copyType       = req->copyType;
            cmd.mapId          = req->mapId;
            cmd.ownerId        = req->ownerId;
            cmd.maxPlayer      = req->maxPlayer;
            copyWireField(cmd.mapName, req->mapName);
            copyWireField(cmd.mapFile, req->mapFile);

            ConnID targetConn = m_sceneManager.findConnBySceneServerId(targetId);
            if (targetConn != INVALID_CONN_ID)
            {
                m_server.SendMsg(targetConn,
                                 (uint16_t)InternalMsgID::SES_COPY_CREATE_CMD,
                                 reinterpret_cast<char*>(&cmd), sizeof(cmd));
            }
            else
            {
                LOG_WARN("CopyCreateCmd: target SceneServer %u not connected", targetId);
                rsp.code = -1;
            }
        }

        m_server.SendMsg(fromConn, (uint16_t)InternalMsgID::SES_COPY_CREATE_RSP,
                          reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    void AutoSaveAll()
    {
        m_userManager.forEach([this](UserID uid, const std::shared_ptr<SessionUser>& user)
        {
            (void)uid;
            if (user->needSave())
                user->save(m_db);
        });
    }

    TcpServer             m_server;
    TcpClient             m_superClient;
    MYSQL*                m_db;
    uint32_t              m_hbSeq = 0;
    SessionUserManager    m_userManager;
    SessionSceneManager   m_sceneManager;
};
