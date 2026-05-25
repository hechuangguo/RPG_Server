/**
 * @file    SceneServer.h
 * @brief  场景服务器 —— 在线用户数据管理、地图逻辑、Lua 脚本执行
 *
 * ## 职责
 * - 管理用户在线状态与地图实例
 * - 处理客户端游戏消息（移动、聊天、技能、心跳）
 * - 内嵌 Lua 虚拟机执行游戏脚本（NPC、任务、技能等）
 * - 与 AOIServer 协同管理视野
 *
 * ## 依赖关系
 * - 依赖 SuperServer / SessionServer / RecordServer / AOIServer / GatewayServer
 * - 可选连接 GlobalServer / ZoneServer
 * - 支持多进程负载均衡（多 SceneServer 承载不同地图）
 *
 * ## Lua 集成
 * LuaManager 负责虚拟机与 C++→Lua 调用；ScriptFun 注册 Lua→C++ 接口。
 * - log_info(msg)     : 输出日志
 * - send_to_user(userID, msgID, data) : 向客户端发送消息
 * - SceneEntry userdata : entry:getEntryId() 等
 *
 * Lua 回调约定：
 * - OnUserEnter(userID, mapID) : 用户进入场景
 * - OnUserLeave(userID)         : 用户离开场景
 * - OnTick(nowMs)               : 每帧回调
 * - OnSkillReq(connID, data)    : 技能请求
 * - OnMsg_XXXX(connID, data)    : 自定义消息处理（XXXX 为十六进制 msgID）
 */

#pragma once
#include "../sdk/net/TcpServer.h"
#include "../sdk/net/TcpClient.h"
#include "../sdk/util/MsgDispatcher.h"
#include "../sdk/util/SceneInfoLoader.h"
#include "../sdk/util/ConfigLoader.h"
#include "../sdk/log/Logger.h"
#include "../sdk/timer/TimerMgr.h"
#include "../protocal/InternalMsg.h"
#include "../common/ClientMsg.h"
#include "../sdk/util/UserWireUtil.h"
#include "../sdk/util/WireStringUtil.h"
#include "SceneUser.h"
#include "SceneUserManager.h"
#include "SceneNpcManager.h"
#include "SceneManager.h"
#include "Scene.h"
#include "LuaManager.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

/**
 * @brief SceneServer 核心类
 *
 * 连接最复杂的服务器，与几乎所有其他服务器有通信关系。
 */
class SceneServer : public INetCallback
{
public:
    static SceneServer* Instance() { return s_instance; }

    SceneServer()
        : m_server(this)
        , m_superClient(this)
        , m_sessionClient(this)
        , m_recordClient(this)
        , m_aoiClient(this)
        , m_gatewayClient(this)
        , m_globalClient(this)
        , m_zoneClient(this)
        , m_sceneID(0)
    {}
    ~SceneServer() = default;

    /** @brief 供 ScriptFun 等 Lua 绑定查询在线用户 */
    std::shared_ptr<SceneUser> findUser(UserID userId) const
    {
        return m_userManager.findUser(userId);
    }

    /** @brief 供 ScriptFun 向客户端发消息 */
    void sendToClient(uint32_t clientConnId, uint16_t msgId,
                      const char* data, uint16_t len)
    {
        SendToClient(clientConnId, msgId, data, len);
    }

    LuaManager& getLuaManager() { return m_luaMgr; }
    const LuaManager& getLuaManager() const { return m_luaMgr; }

    /**
     * @brief 初始化 SceneServer
     * @param ip        监听 IP
     * @param port      监听端口
     * @param cfg       全局配置
     * @param sceneInfo 场景地图配置
     * @return 成功返回 true
     */
    bool Init(const std::string& ip, uint16_t port,
              const ServerConfig& cfg, const SceneServerInfo& sceneInfo)
    {
        Logger::Instance().SetServerName("SceneServer");
        m_sceneID = sceneInfo.sceneID;
        s_instance = this;
        if (!m_server.Start(ip, port)) { LOG_FATAL("SceneServer start failed"); return false; }

        // ── 连接各依赖服务器 ──
        m_superClient.Connect(cfg.superIP,       (uint16_t)cfg.superPort);
        m_sessionClient.Connect("127.0.0.1",     (uint16_t)cfg.sessionPort);
        m_recordClient.Connect("127.0.0.1",      (uint16_t)cfg.recordPort);
        m_aoiClient.Connect("127.0.0.1",         (uint16_t)cfg.aoiPort);
        m_gatewayClient.Connect("127.0.0.1",     (uint16_t)(cfg.gatewayPort + 10000));
        m_listenPort = port;

        m_sceneManager.setStartedCallback([this](Scene& scene) { onSceneStarted(scene); });
        m_sceneManager.setStoppedCallback([this](Scene& scene) { onSceneStopped(scene); });

        if (!m_sceneManager.createNormalScenesFromConfig(m_sceneID, sceneInfo))
            LOG_WARN("Some normal scenes failed to start on SceneServer %u", m_sceneID);

        initMapNpcs();

        if (!m_luaMgr.init())
            LOG_WARN("SceneServer Lua init failed");
        RegisterHandlers();

        /** @brief 定时向 SuperServer 发起注册，确保服务发现 */
        TimerMgr::Instance().Register(500,   0,    [this]{ RegisterToSuper(); });
        /** @brief 每 10 秒发送一次心跳，维持与 SuperServer 的连接存活 */
        TimerMgr::Instance().Register(10000, 10000,[this]{ SendHeartbeat(); });
        /** @brief 每 1 秒触发一次逻辑 Tick，驱动用户 OnTick 与 Lua OnTick */
        TimerMgr::Instance().Register(1000,  1000, [this]{ OnTick(); });

        LOG_INFO("SceneServer %u started on %s:%d", m_sceneID, ip.c_str(), port);
        return true;
    }

    /** @brief 主循环：轮询所有连接的 epoll + 驱动定时器 */
    void Run()
    {
        while (true)
        {
            m_superClient.Poll(0);
            m_sessionClient.Poll(0);
            m_recordClient.Poll(0);
            m_aoiClient.Poll(0);
            m_gatewayClient.Poll(0);
            m_globalClient.Poll(0);
            m_zoneClient.Poll(0);
            m_server.Poll(10);
            TimerMgr::Instance().Update();
        }
    }

    /** @brief NPC 进入 AOI（创建/复活时由 SceneNpc 调用） */
    void notifyNpcEnterAoi(const SceneNpc& npc) { sendAoiEnter(npc, 1); }

    /** @brief NPC 离开 AOI（死亡/销毁时由 SceneNpc 调用） */
    void notifyNpcLeaveAoi(EntryID npcId) { sendAoiLeave(npcId); }

    /** @brief 请求 SessionServer 创建副本（异步，结果见 OnCopyCreateRsp） */
    void requestCreateCopy(CopyType copyType, uint32_t mapId, uint64_t ownerId,
                           const std::string& mapName, const std::string& mapFile,
                           uint32_t maxPlayer = 5)
    {
        Msg_SES_CopyCreateReq req{};
        req.reqSceneServerId = m_sceneID;
        req.copyType         = static_cast<uint32_t>(copyType);
        req.mapId            = mapId;
        req.ownerId          = ownerId;
        req.maxPlayer        = maxPlayer;
        copyToWire(req.mapName, sizeof(req.mapName), mapName.c_str());
        copyToWire(req.mapFile, sizeof(req.mapFile), mapFile.c_str());
        m_sessionClient.SendMsg((uint16_t)InternalMsgID::SES_COPY_CREATE_REQ,
                                 reinterpret_cast<char*>(&req), sizeof(req));
        LOG_INFO("CopyCreateReq sent: type=%u map=%u owner=%llu",
                 req.copyType, mapId, ownerId);
    }

    void OnConnect(ConnID /*id*/)    override {}
    void OnDisconnect(ConnID id) override { LOG_WARN("SceneServer conn lost=%u", id); }
    void OnMessage(ConnID id, uint16_t msgID, const char* data, uint16_t len) override
    {
        MsgDispatcher::Instance().Dispatch(id, msgID, data, len);
    }

private:
    void RegisterHandlers()
    {
        auto& d = MsgDispatcher::Instance();
        /** @brief 注册用户进入场景消息处理 */
        d.Register((uint16_t)InternalMsgID::SCE_USER_ENTER_REQ,
            [this](uint32_t c, const char* d, uint16_t l){ OnUserEnter(c, d, l); });
        /** @brief 注册用户离开场景消息处理 */
        d.Register((uint16_t)InternalMsgID::SCE_USER_LEAVE,
            [this](uint32_t c, const char* d, uint16_t l){ OnUserLeave(c, d, l); });
        /** @brief 注册 Gateway 转发的客户端消息处理 */
        d.Register((uint16_t)InternalMsgID::GW_CLIENT_MSG,
            [this](uint32_t c, const char* d, uint16_t l){ OnClientMsg(c, d, l); });
        /** @brief 注册 AOI 视野变化通知处理 */
        d.Register((uint16_t)InternalMsgID::AOI_VIEW_NOTIFY,
            [this](uint32_t c, const char* d, uint16_t l){ OnViewNotify(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SES_COPY_CREATE_RSP,
            [this](uint32_t c, const char* d, uint16_t l){ OnCopyCreateRsp(c, d, l); });
        d.Register((uint16_t)InternalMsgID::SES_COPY_CREATE_CMD,
            [this](uint32_t c, const char* d, uint16_t l){ OnCopyCreateCmd(c, d, l); });
    }

    /**
     * @brief 用户进入场景
     *
     * 创建 SceneUser → 加入地图 → 通知 AOI → 回包 GatewayServer → 调用 Lua OnUserEnter。
     */
    void OnUserEnter(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_SCE_UserEnterReq)) return;
        const auto* req = reinterpret_cast<const Msg_SCE_UserEnterReq*>(data);
        LOG_INFO("UserEnter: userID=%llu mapID=%u clientConn=%u",
                 req->userID, req->mapID, req->gatewayClientConnID);

        uint32_t mapID = req->mapID ? req->mapID : 1001;
        auto scene = m_sceneManager.findNormalSceneByMapId(mapID);
        if (!scene)
        {
            LOG_WARN("Map %u not found on SceneServer %u", mapID, m_sceneID);
            SendUserEnterRsp(req, -1);
            return;
        }

        UserBase base;
        base.userID   = req->userID;
        base.name     = req->name;
        base.level    = req->level;
        base.vocation = req->vocation;
        base.sex      = req->sex;
        base.mapID    = mapID;
        base.posX     = req->x;
        base.posY     = req->y;
        base.posZ     = req->z;
        base.hp       = req->hp;
        base.maxHP    = req->maxHP;
        base.mp       = req->mp;
        base.maxMP    = req->maxMP;
        base.gold     = req->gold;

        auto user = SceneUser::create(base);
        user->init();
        user->load();
        user->setGatewayClientConn(req->gatewayClientConnID);
        user->onOnline();
        m_userManager.addUser(req->userID, user);
        scene->addPlayer(req->userID);

        Msg_AOI_Move aoi{};
        aoi.entityID   = req->userID;
        aoi.mapID      = mapID;
        aoi.x = req->x;
        aoi.y = req->y;
        aoi.z = req->z;
        aoi.entityType = 0;
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_ENTER_REQ,
                             reinterpret_cast<char*>(&aoi), sizeof(aoi));

        NotifyExistingPlayersOnEnter(*user);
        SendUserEnterRsp(req, 0);
        CallLuaOnEnter(req->userID, mapID);
    }

    void SendUserEnterRsp(const Msg_SCE_UserEnterReq* req, int32_t code)
    {
        Msg_SCE_UserEnterRsp rsp{};
        rsp.code                = code;
        rsp.userID              = req->userID;
        rsp.gatewayClientConnID = req->gatewayClientConnID;
        rsp.mapID               = req->mapID ? req->mapID : 1001;
        m_superClient.SendMsg((uint16_t)InternalMsgID::SCE_USER_ENTER_RSP,
                              reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    void NotifyExistingPlayersOnEnter(const SceneUser& entering)
    {
        const auto& base = entering.Base();
        Msg_S2C_SpawnEntity spawn{};
        fillSpawnFromEntry(entering, 0, spawn);

        m_userManager.forEach([&](UserID rid, const std::shared_ptr<SceneUser>& user)
        {
            if (rid == base.userID) return;
            if (user->Base().mapID != base.mapID) return;
            if (user->getGatewayClientConn() == 0) return;

            SendToClient(user->getGatewayClientConn(),
                         (uint16_t)ClientMsgID::S2C_SPAWN_ENTITY,
                         reinterpret_cast<char*>(&spawn), sizeof(spawn));

            Msg_S2C_SpawnEntity other{};
            fillSpawnFromEntry(*user, 0, other);
            SendToClient(entering.getGatewayClientConn(),
                         (uint16_t)ClientMsgID::S2C_SPAWN_ENTITY,
                         reinterpret_cast<char*>(&other), sizeof(other));
        });

        m_npcManager.forEach([&](EntryID /*npcId*/, const std::shared_ptr<SceneNpc>& npc)
        {
            if (!npc || npc->getMapId() != base.mapID) return;
            if (!npc->isAlive()) return;

            Msg_S2C_SpawnEntity npcSpawn{};
            fillSpawnFromEntry(*npc, 1, npcSpawn);
            SendToClient(entering.getGatewayClientConn(),
                         (uint16_t)ClientMsgID::S2C_SPAWN_ENTITY,
                         reinterpret_cast<char*>(&npcSpawn), sizeof(npcSpawn));
        });
    }

    /** @brief 用户离开场景（通知 AOI → 保存到 RecordServer → 调用 Lua → 清理内存） */
    void OnUserLeave(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(UserID)) return;
        UserID uid = *reinterpret_cast<const UserID*>(data);
        auto user = m_userManager.findUser(uid);
        if (!user) return;

        if (auto scene = m_sceneManager.findNormalSceneByMapId(user->Base().mapID))
            scene->removePlayer(uid);

        user->onOffline();
        if (user->needSave())
        {
            sendCharBaseToRecord(*user);
            user->save();
        }

        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_LEAVE_REQ,
                             reinterpret_cast<const char*>(&uid), sizeof(uid));
        CallLuaOnLeave(uid);
        m_userManager.removeUser(uid);
        LOG_INFO("UserLeave: userID=%llu", uid);
    }

    /** @brief 将 Scene 在线数据转发 RecordServer 写入 t_charbase */
    void sendCharBaseToRecord(const SceneUser& user)
    {
        Msg_REC_SaveUserReq req{};
        req.userID = user.GetID();
        req.wire   = toUserBaseWire(user.Base());
        m_recordClient.SendMsg((uint16_t)InternalMsgID::REC_SAVE_USER_REQ,
                               reinterpret_cast<char*>(&req), sizeof(req));
    }

    /** @brief 处理 GatewayServer 转发来的客户端消息 */
    void OnClientMsg(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_GW_ClientMsg)) return;
        const auto* hdr = reinterpret_cast<const Msg_GW_ClientMsg*>(data);
        const char* body = data + sizeof(Msg_GW_ClientMsg);
        uint16_t bodyLen = hdr->dataLen;
        LOG_DEBUG("ClientMsg: connID=%u msgID=0x%04X", hdr->clientConnID, hdr->msgID);
        HandleClientMsg(hdr->clientConnID, hdr->msgID, body, bodyLen);
    }

    /**
     * @brief 处理 AOI 视野变化通知
     *
     * 根据消息长度区分两种场景：
     * - 若 len == sizeof(Msg_AOI_Move)：为移动坐标更新，直接同步实体位置并向同地图广播移动通知；
     * - 否则为视野进入/离开通知，解析 entityID + enter 标志位：
     *   - enter=true：向视野内其他用户广播 SpawnEntity（新实体出现）；
     *   - enter=false：向视野内其他用户广播 DespawnEntity（实体消失）。
     */
    void OnViewNotify(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len == sizeof(Msg_AOI_Move))
        {
            const auto* move = reinterpret_cast<const Msg_AOI_Move*>(data);
            auto user = m_userManager.findUser(move->entityID);
            if (user)
            {
                user->Base().posX = move->x;
                user->Base().posY = move->y;
                user->Base().posZ = move->z;
                user->markDirty();
            }
            else
            {
                auto npc = m_npcManager.findNpc(move->entityID);
                if (npc)
                    npc->setPos(move->x, move->y, move->z);
            }

            Msg_S2C_MoveNotify notify{};
            notify.userID   = move->entityID;
            notify.x        = move->x;
            notify.y        = move->y;
            notify.z        = move->z;
            notify.dir      = move->dir;
            notify.moveType = 0;
            BroadcastToMap(move->mapID, move->entityID,
                           (uint16_t)ClientMsgID::S2C_MOVE_NOTIFY,
                           reinterpret_cast<char*>(&notify), sizeof(notify));
            return;
        }

        if (len < sizeof(uint64_t) + 1) return;
        uint64_t entityID = 0;
        memcpy(&entityID, data, sizeof(uint64_t));
        bool enter = data[sizeof(uint64_t)] != 0;

        auto user = m_userManager.findUser(entityID);
        auto npc  = m_npcManager.findNpc(entityID);

        uint32_t mapId = 0;
        if (user)
            mapId = user->Base().mapID;
        else if (npc)
            mapId = npc->getMapId();
        else
            return;

        if (enter)
        {
            Msg_S2C_SpawnEntity spawn{};
            if (user)
                fillSpawnFromEntry(*user, 0, spawn);
            else
                fillSpawnFromEntry(*npc, 1, spawn);

            BroadcastToMap(mapId, entityID,
                           (uint16_t)ClientMsgID::S2C_SPAWN_ENTITY,
                           reinterpret_cast<char*>(&spawn), sizeof(spawn));
        }
        else
        {
            Msg_S2C_DespawnEntity despawn{};
            despawn.entityID = entityID;
            BroadcastToMap(mapId, entityID,
                           (uint16_t)ClientMsgID::S2C_DESPAWN_ENTITY,
                           reinterpret_cast<char*>(&despawn), sizeof(despawn));
        }
    }

    /**
     * @brief 客户端消息分发路由
     *
     * 采用 switch-case 路由策略：已知协议号直接映射到对应的处理函数，
     * 未知协议号统一委派给 Lua 脚本处理（OnMsg_XXXX）。
     * 路由优先级：C++ 原生处理 > Lua 脚本扩展处理。
     */
    void HandleClientMsg(uint32_t clientConnID, uint16_t msgID,
                         const char* data, uint16_t len)
    {
        using CID = ClientMsgID;
        switch ((CID)msgID)
        {
        case CID::C2S_MOVE_REQ:   OnMoveReq(clientConnID, data, len); break;
        case CID::C2S_CHAT_REQ:   OnChatReq(clientConnID, data, len); break;
        case CID::C2S_SKILL_REQ:  OnSkillReq(clientConnID, data, len); break;
        case CID::C2S_NPC_TALK_REQ: OnNpcTalkReq(clientConnID, data, len); break;
        case CID::C2S_HEARTBEAT:  OnHeartbeatReq(clientConnID, data, len); break;
        default:                  CallLuaMsgHandler(clientConnID, msgID, data, len);
        }
    }

    /**
     * @brief 处理移动请求：坐标验证 → 位置更新 → 通知 AOI
     *
     * 移动验证流程：
     * 1. 根据消息中的 userID 查找在线用户，不存在则忽略；
     * 2. 将客户端上报的坐标 (x, y, z) 更新到用户的基础数据中；
     * 3. 构造 AOI 移动消息并转发至 AOIServer，由 AOIServer 计算视野变化
     *    并回传 OnViewNotify，触发同地图其他客户端的坐标同步。
     *
     * @note 当前未做坐标合法性校验（如移动速度检测），后续可扩展。
     */
    void OnMoveReq(uint32_t /*clientConnID*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_MoveReq)) return;
        const auto* req = reinterpret_cast<const Msg_C2S_MoveReq*>(data);
        auto user = m_userManager.findUser(req->userID);
        if (!user) return;
        user->Base().posX = req->x;
        user->Base().posY = req->y;
        user->Base().posZ = req->z;
        user->markDirty();

        Msg_AOI_Move aoi{};
        aoi.entityID = req->userID;
        aoi.mapID    = user->Base().mapID;
        aoi.x = req->x; aoi.y = req->y; aoi.z = req->z; aoi.dir = req->dir;
        aoi.entityType = 0;
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_MOVE_REQ,
                             reinterpret_cast<char*>(&aoi), sizeof(aoi));
    }

    /** @brief 处理聊天请求：广播给地图内所有玩家 */
    void OnChatReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_Chat)) return;
        const auto* req = reinterpret_cast<const Msg_C2S_Chat*>(data);
        auto user = m_userManager.findUserByClientConn(clientConnID);
        if (!user) return;

        Msg_S2C_Chat notify{};
        notify.fromID = user->GetID();
        notify.channel = req->channel;
        snprintf(notify.fromName, sizeof(notify.fromName), "%s", user->GetName());
        snprintf(notify.content, sizeof(notify.content), "%s", req->content);
        BroadcastToMap(user->Base().mapID, user->GetID(),
                       (uint16_t)ClientMsgID::S2C_CHAT_NOTIFY,
                       reinterpret_cast<char*>(&notify), sizeof(notify));
    }

    /** @brief 处理技能请求：委派 Lua 处理 */
    void OnSkillReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        LOG_DEBUG("SkillReq from conn=%u", clientConnID);
        CallLuaSkillHandler(clientConnID, data, len);
    }

    /**
     * @brief 处理 NPC 对话：callScriptBool → OnNpcTalk → guide.lua 等
     *
     * 成功时由 Lua send_npc_talk_rsp 下发 S2C_NPC_TALK_RSP；
     * 失败时 C++ 回错误码包。
     */
    void OnNpcTalkReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_C2S_NpcTalkReq))
            return;

        const auto* req = reinterpret_cast<const Msg_C2S_NpcTalkReq*>(data);
        auto user = m_userManager.findUserByClientConn(clientConnID);
        if (!user || user->getGatewayClientConn() == 0)
            return;

        auto npc = m_npcManager.findNpc(req->npcId);
        if (!npc || npc->isDead())
        {
            sendNpcTalkError(user->getGatewayClientConn(), req->npcId, 1);
            return;
        }

        if (npc->getMapId() != user->Base().mapID)
        {
            sendNpcTalkError(user->getGatewayClientConn(), req->npcId, 2);
            return;
        }

        const bool ok = m_luaMgr.callScriptBool(npc.get(), "OnNpcTalk", {
            LuaArg::integer(static_cast<int64_t>(user->GetID())),
            LuaArg::integer(req->dialogStep),
            LuaArg::integer(npc->getTemplateId()),
        });

        if (!ok)
            sendNpcTalkError(user->getGatewayClientConn(), req->npcId, 3);
    }

    /** @brief 对话失败回包（code: 1=NPC无效 2=不同地图 3=脚本无响应） */
    void sendNpcTalkError(uint32_t clientConnID, uint64_t npcId, int32_t code)
    {
        Msg_S2C_NpcTalkRsp rsp{};
        rsp.code   = code;
        rsp.npcId  = npcId;
        rsp.dialogStep = 0;
        rsp.optionCount = 0;
        SendToClient(clientConnID, (uint16_t)ClientMsgID::S2C_NPC_TALK_RSP,
                     reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    /** @brief 处理心跳请求：回包服务器时间 */
    void OnHeartbeatReq(uint32_t clientConnID, const char* data, uint16_t len)
    {
        Msg_S2C_Heartbeat rsp{};
        if (len >= sizeof(Msg_C2S_Heartbeat))
            rsp.seq = reinterpret_cast<const Msg_C2S_Heartbeat*>(data)->seq;
        rsp.serverTime = TimerMgr::NowMs();
        SendToClient(clientConnID, (uint16_t)ClientMsgID::S2C_HEARTBEAT,
                     reinterpret_cast<char*>(&rsp), sizeof(rsp));
    }

    /**
     * @brief 向客户端发送消息（通过 GatewayServer 转发）
     *
     * 包格式：[clientConnID(4)][msgID(2)][data...]
     */
    void SendToClient(uint32_t clientConnID, uint16_t msgID, const char* data, uint16_t len)
    {
        std::vector<char> buf(sizeof(uint32_t) + sizeof(uint16_t) + len);
        memcpy(buf.data(), &clientConnID, sizeof(uint32_t));
        memcpy(buf.data() + sizeof(uint32_t), &msgID, sizeof(uint16_t));
        if (len > 0) memcpy(buf.data() + sizeof(uint32_t) + sizeof(uint16_t), data, len);
        m_gatewayClient.SendMsg((uint16_t)InternalMsgID::GW_SEND_TO_CLIENT,
                                 buf.data(), (uint16_t)buf.size());
    }

    /**
     * @brief 广播消息给指定地图内的所有用户（可排除指定用户）
     *
     * 广播排除逻辑：
     * 1. 遍历所有在线用户 m_users；
     * 2. 跳过不在目标 mapID 中的用户（mapID==0 时广播所有地图）；
     * 3. 跳过 excludeUserID 指定的用户（通常为消息发起者，避免自己收到自己的广播）；
     * 4. 跳过 gatewayClientConn==0 的用户（连接无效，无法发送）；
     * 5. 对剩余用户逐一调用 SendToClient 转发消息。
     *
     * @param mapID          目标地图 ID，0 表示所有地图
     * @param excludeUserID  需要排除的用户 ID
     * @param msgID          消息协议号
     * @param data           消息体指针
     * @param len            消息体长度
     */
    void BroadcastToMap(uint32_t mapID, UserID excludeUserID,
                        uint16_t msgID, const char* data, uint16_t len)
    {
        m_userManager.forEach([&](UserID uid, const std::shared_ptr<SceneUser>& user)
        {
            if (mapID != 0 && user->Base().mapID != mapID) return;
            if (uid == excludeUserID) return;
            if (user->getGatewayClientConn() == 0) return;
            SendToClient(user->getGatewayClientConn(), msgID, data, len);
        });
    }

    /** @brief Lua 回调：用户进入场景 */
    void CallLuaOnEnter(UserID userID, uint32_t mapID)
    {
        m_luaMgr.callGlobalVoid("OnUserEnter", {
            LuaArg::integer(static_cast<int64_t>(userID)),
            LuaArg::integer(mapID),
        });
    }

    /** @brief Lua 回调：用户离开场景 */
    void CallLuaOnLeave(UserID userID)
    {
        m_luaMgr.callGlobalVoid("OnUserLeave", {
            LuaArg::integer(static_cast<int64_t>(userID)),
        });
    }

    /**
     * @brief Lua 回调：通用消息处理（OnMsg_XXXX）
     *
     * 根据 msgID 拼全局函数名；connID + 二进制 data 作为参数。
     */
    void CallLuaMsgHandler(uint32_t connID, uint16_t msgID, const char* data, uint16_t len)
    {
        char funcName[32];
        snprintf(funcName, sizeof(funcName), "OnMsg_%04X", msgID);
        m_luaMgr.callGlobalVoid(funcName, {
            LuaArg::integer(connID),
            LuaArg::binary(data, len),
        });
    }

    /** @brief Lua 回调：技能请求 */
    void CallLuaSkillHandler(uint32_t connID, const char* data, uint16_t len)
    {
        m_luaMgr.callGlobalVoid("OnSkillReq", {
            LuaArg::integer(connID),
            LuaArg::binary(data, len),
        });
    }

    /** @brief 每帧 Tick（驱动用户/NPC loop + Lua OnTick） */
    void OnTick()
    {
        uint64_t now = TimerMgr::NowMs();
        m_userManager.forEachMutable([&](UserID /*uid*/, SceneUser& user)
        {
            user.loop(now);
        });
        m_npcManager.loopAll(now);
        m_luaMgr.callGlobalVoid("OnTick", { LuaArg::integer(static_cast<int64_t>(now)) });
    }

    /** @brief 为已加载地图创建默认 NPC（示例：新手引导官） */
    void initMapNpcs()
    {
        m_sceneManager.forEach([this](const std::shared_ptr<Scene>& scene)
        {
            if (!scene || scene->getSceneKind() != SceneKind::NORMAL)
                return;

            const uint32_t mapId = scene->getMapId();
            SceneNpcDef def{};
            def.npcId       = 1000000ULL + mapId;
            def.templateId  = 1;
            def.name        = "新手引导官";
            def.level       = 1;
            def.hp          = 500;
            def.maxHp       = 500;
            def.vitality    = 100;
            def.maxVitality = 100;
            def.mapId       = mapId;
            def.posX        = 10.f;
            def.posY        = 0.f;
            def.posZ        = 10.f;
            def.respawnSec  = 30;

            if (m_npcManager.createNpc(def))
            {
                LOG_INFO("NPC spawned: id=%llu map=%u", def.npcId, mapId);
                auto npc = m_npcManager.findNpc(def.npcId);
                if (npc)
                    notifyNpcEnterAoi(*npc);
            }
        });
    }

    /** @brief 场景启动成功：注册 AOI + SessionServer */
    void onSceneStarted(Scene& scene)
    {
        Msg_AOI_SceneRegister aoiReg{};
        aoiReg.sceneServerId   = m_sceneID;
        aoiReg.sceneInstanceId = scene.getSceneInstanceId();
        aoiReg.mapId           = scene.getMapId();
        aoiReg.sceneKind       = static_cast<uint8_t>(scene.getSceneKind());
        aoiReg.maxPlayer       = scene.getMaxPlayer();
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_SCENE_REGISTER,
                             reinterpret_cast<char*>(&aoiReg), sizeof(aoiReg));

        Msg_SES_SceneRegisterReq sesReg{};
        sesReg.sceneServerId   = m_sceneID;
        sesReg.sceneInstanceId = scene.getSceneInstanceId();
        sesReg.mapId           = scene.getMapId();
        sesReg.sceneKind       = static_cast<uint8_t>(scene.getSceneKind());
        sesReg.maxPlayer       = scene.getMaxPlayer();
        copyToWire(sesReg.mapName, sizeof(sesReg.mapName), scene.getMapName().c_str());
        copyToWire(sesReg.mapFile, sizeof(sesReg.mapFile), scene.getMapFile().c_str());
        m_sessionClient.SendMsg((uint16_t)InternalMsgID::SES_SCENE_REGISTER_REQ,
                                 reinterpret_cast<char*>(&sesReg), sizeof(sesReg));

        LOG_INFO("Scene registered AOI+Session: instance=%llu map=%u kind=%u",
                 scene.getSceneInstanceId(), scene.getMapId(),
                 static_cast<unsigned>(scene.getSceneKind()));
    }

    /** @brief 场景关闭：注销 AOI + SessionServer */
    void onSceneStopped(Scene& scene)
    {
        Msg_AOI_SceneUnregister aoiUnreg{};
        aoiUnreg.sceneInstanceId = scene.getSceneInstanceId();
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_SCENE_UNREGISTER,
                             reinterpret_cast<char*>(&aoiUnreg), sizeof(aoiUnreg));

        Msg_SES_SceneUnregister sesUnreg{};
        sesUnreg.sceneInstanceId = scene.getSceneInstanceId();
        sesUnreg.sceneServerId   = m_sceneID;
        m_sessionClient.SendMsg((uint16_t)InternalMsgID::SES_SCENE_UNREGISTER,
                                 reinterpret_cast<char*>(&sesUnreg), sizeof(sesUnreg));
    }

    void OnCopyCreateRsp(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_SES_CopyCreateRsp)) return;
        const auto* rsp = reinterpret_cast<const Msg_SES_CopyCreateRsp*>(data);
        LOG_INFO("CopyCreateRsp: code=%d targetServer=%u instance=%llu reused=%u",
                 rsp->code, rsp->targetSceneServerId, rsp->copyInstanceId, rsp->reused);
    }

    void OnCopyCreateCmd(ConnID /*fromConn*/, const char* data, uint16_t len)
    {
        if (len < sizeof(Msg_SES_CopyCreateCmd)) return;
        const auto* cmd = reinterpret_cast<const Msg_SES_CopyCreateCmd*>(data);

        CopySceneDef def{};
        def.copyInstanceId = cmd->copyInstanceId;
        def.copyType       = static_cast<CopyType>(cmd->copyType);
        def.mapId          = cmd->mapId;
        def.ownerId        = cmd->ownerId;
        def.maxPlayer      = cmd->maxPlayer;
        def.mapName        = cmd->mapName;
        def.mapFile        = cmd->mapFile;

        if (m_sceneManager.createCopyScene(m_sceneID, def))
            LOG_INFO("CopyScene created locally: instance=%llu", cmd->copyInstanceId);
        else
            LOG_ERR("CopyScene create failed: instance=%llu", cmd->copyInstanceId);
    }

    /** @brief 将 SceneEntry 填入客户端 SpawnEntity 协议 */
    static void fillSpawnFromEntry(const SceneEntry& entry, uint8_t entityType,
                                   Msg_S2C_SpawnEntity& spawn)
    {
        spawn.entityID   = entry.getEntryId();
        spawn.level      = entry.getLevel();
        spawn.x          = entry.getPosX();
        spawn.y          = entry.getPosY();
        spawn.z          = entry.getPosZ();
        spawn.dir        = 0.f;
        spawn.entityType = entityType;
        copyToWire(spawn.name, sizeof(spawn.name), entry.getName().c_str());
    }

    /** @brief 实体进入 AOIServer 视野管理 */
    void sendAoiEnter(const SceneEntry& entry, uint8_t entityType)
    {
        Msg_AOI_Move aoi{};
        aoi.entityID   = entry.getEntryId();
        aoi.mapID      = entry.getMapId();
        aoi.x          = entry.getPosX();
        aoi.y          = entry.getPosY();
        aoi.z          = entry.getPosZ();
        aoi.dir        = 0.f;
        aoi.entityType = entityType;
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_ENTER_REQ,
                             reinterpret_cast<char*>(&aoi), sizeof(aoi));
    }

    /** @brief 实体离开 AOIServer 视野管理 */
    void sendAoiLeave(EntryID entityId)
    {
        m_aoiClient.SendMsg((uint16_t)InternalMsgID::AOI_LEAVE_REQ,
                             reinterpret_cast<const char*>(&entityId), sizeof(entityId));
    }

    void RegisterToSuper()
    {
        Msg_S2S_Register reg{};
        reg.serverType = (uint8_t)SubServerType::SCENE;
        reg.serverID   = m_sceneID;
        copyToWire(reg.ip, sizeof(reg.ip), "127.0.0.1");
        reg.port       = m_listenPort;
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_REGISTER_REQ,
                               reinterpret_cast<char*>(&reg), sizeof(reg));
    }

    void SendHeartbeat()
    {
        Msg_S2S_Heartbeat hb{}; hb.seq = ++m_hbSeq; hb.timestamp = TimerMgr::NowMs();
        m_superClient.SendMsg((uint16_t)InternalMsgID::S2S_HEARTBEAT,
                               reinterpret_cast<char*>(&hb), sizeof(hb));
    }

    TcpServer  m_server;          /**< 内部连接监听 */
    TcpClient  m_superClient;     /**< 到 SuperServer */
    TcpClient  m_sessionClient;   /**< 到 SessionServer */
    TcpClient  m_recordClient;    /**< 到 RecordServer */
    TcpClient  m_aoiClient;       /**< 到 AOIServer */
    TcpClient  m_gatewayClient;   /**< 到 GatewayServer */
    TcpClient  m_globalClient;    /**< 到 GlobalServer（可选） */
    TcpClient  m_zoneClient;      /**< 到 ZoneServer（可选） */
    LuaManager m_luaMgr;          /**< Lua 虚拟机与 C++↔脚本桥接 */
    uint32_t   m_sceneID;         /**< 场景服务器编号 */
    uint32_t   m_hbSeq = 0;       /**< 心跳序列号 */
    uint16_t   m_listenPort = 9004;

    inline static SceneServer* s_instance = nullptr;

    /** @brief 地图实例管理 */
    SceneManager                                          m_sceneManager;
    SceneUserManager                                      m_userManager;
    SceneNpcManager                                       m_npcManager;
};
