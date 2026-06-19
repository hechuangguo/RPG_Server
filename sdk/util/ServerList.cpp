/**
 * @file    ServerList.cpp
 * @brief   集群拓扑 ServerList 容器与启动期拉取客户端实现
 */

#include "ServerList.h"

#include "../sdk/util/ServerList.h"

#include "../net/NetTls.h"
#include "../net/TcpClient.h"
#include "../net/MsgId.h"

#include <chrono>
#include <cstring>
#include <thread>

// ============================================================
//  ServerList 容器
// ============================================================
void ServerList::add(const ServerEntry& entry)
{
    m_entries.push_back(entry);
}

void ServerList::clear()
{
    m_entries.clear();
}

size_t ServerList::size() const
{
    return m_entries.size();
}

const std::vector<ServerEntry>& ServerList::all() const
{
    return m_entries;
}

const ServerEntry* ServerList::find(SubServerType type, uint32_t id) const
{
    for (const auto& e : m_entries)
    {
        if (e.type == type && e.id == id)
        {
            return &e;
        }
    }
    return nullptr;
}

const ServerEntry* ServerList::findFirst(SubServerType type) const
{
    for (const auto& e : m_entries)
    {
        if (e.type == type)
        {
            return &e;
        }
    }
    return nullptr;
}

void ServerList::findAll(SubServerType type, std::vector<const ServerEntry*>& out) const
{
    out.clear();
    for (const auto& e : m_entries)
    {
        if (e.type == type)
            out.push_back(&e);
    }
}

// ============================================================
//  ServerListClient::fetch —— 启动期同步拉取
// ============================================================
namespace
{
/**
 * @brief fetch 内部回调：捕获 S2S_SERVERLIST_RSP 并解析为 ServerList
 */
class FetchCallback : public INetCallback
{
public:
    FetchCallback(ServerList& out) : m_out(out) {}

    void OnConnect(ConnID) override {}
    void OnDisconnect(ConnID) override {}

    void OnMessage(ConnID, uint8_t module, uint8_t sub,
                   const char* data, uint16_t len) override
    {
        if (makeMsgId(module, sub) != (uint16_t)InternalMsgID::S2S_SERVERLIST_RSP)
        {
            return;
        }
        if (len < sizeof(Msg_S2S_ServerListRsp))
        {
            m_done = true;  // 收到空/异常响应也结束等待
            return;
        }
        const auto* hdr = reinterpret_cast<const Msg_S2S_ServerListRsp*>(data);
        uint16_t count = hdr->count;
        const char* p = data + sizeof(Msg_S2S_ServerListRsp);
        uint16_t avail = (uint16_t)(len - sizeof(Msg_S2S_ServerListRsp));
        uint16_t maxByCount = (uint16_t)(avail / sizeof(Msg_ServerEntry));
        if (count > maxByCount)
        {
            count = maxByCount;  // 防御：以实际可解析数量为准
        }
        m_out.clear();
        for (uint16_t i = 0; i < count; ++i)
        {
            Msg_ServerEntry wire{};
            memcpy(&wire, p + (size_t)i * sizeof(Msg_ServerEntry), sizeof(Msg_ServerEntry));
            ServerEntry e;
            e.id   = wire.serverID;
            e.type = (SubServerType)wire.serverType;
            e.ip   = wire.ip;
            e.port = wire.port;
            e.name = wire.name;
            m_out.add(e);
        }
        m_done = true;
    }

    bool done() const { return m_done; }

private:
    ServerList& m_out;       /**< 输出容器 */
    bool        m_done = false; /**< 是否已收到响应 */
};
}  // namespace

bool ServerListClient::fetch(const std::string& superIP, uint16_t superPort,
                             SubServerType selfType, uint32_t selfID,
                             ServerList& out, int timeoutMs)
{
    FetchCallback cb(out);
    TcpClient client(&cb);
    wireTlsClient(client);
    if (!client.Connect(superIP, superPort))
    {
        return false;
    }

    // 请求缓冲后由首个 EPOLLOUT 刷出，无需等待 OnConnect
    Msg_S2S_ServerListReq req{};
    req.serverType = (uint8_t)selfType;
    req.serverID   = selfID;
    client.SendMsg((uint16_t)InternalMsgID::S2S_SERVERLIST_REQ,
                   reinterpret_cast<char*>(&req), sizeof(req));

    const auto start = std::chrono::steady_clock::now();
    while (!cb.done())
    {
        client.Poll(20);
        if (!client.IsConnected())
        {
            break;  // 连接异常断开
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeoutMs)
        {
            break;
        }
    }
    client.Disconnect();
    return cb.done();
}
