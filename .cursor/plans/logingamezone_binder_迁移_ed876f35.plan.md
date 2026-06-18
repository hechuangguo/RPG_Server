---
name: LoginGameZone Binder 迁移
overview: 为 LoginGameZone 五个 *MsgRegister 模块替换手写 `d.Register` lambda，复用 `MsgHandlerBinder`：Service 类走 `registerInternalRaw`，匿名命名空间自由函数新增 `registerInternalFree`（含定长 typed 重载），并顺带去掉 handler 内重复的 `sizeof` 守卫。
todos:
  - id: binder-free-fn
    content: MsgHandlerBinder.h 新增 registerInternalFree / registerInternalFree<BodyT>
    status: completed
  - id: auth-gateway-zone
    content: LoginGameZoneAuth/Gateway/Zone：handler 改 const BodyT& + registerInternalFree
    status: completed
  - id: recharge-gm
    content: LoginGameZoneRecharge/Gm：registerInternalRaw 绑定 Service 方法
    status: completed
  - id: build-login
    content: cmake + 编译 LoginServer；grep 确认无 LoginGameZone d.Register 残留
    status: completed
isProject: false
---

# LoginGameZone *MsgRegister Binder 迁移

## 现状

[`LoginGameZoneMsg.cpp`](LoginServer/LoginGameZoneMsg.cpp) 聚合 5 个子注册器，共 **7 处** `d.Register` + lambda：

| 文件 | 注册数 | Handler 形态 |
|------|--------|--------------|
| [`LoginGameZoneAuthMsg.cpp`](LoginServer/LoginGameZoneAuthMsg.cpp) | 2 | 匿名命名空间 `onVerifyTokenReq` / `onUpdateLastUserReq(LoginServer&, ConnID, const char*, uint16_t)` |
| [`LoginGameZoneGatewayMsg.cpp`](LoginServer/LoginGameZoneGatewayMsg.cpp) | 2 | 匿名命名空间 `onGatewayRegister` / `onGatewayHeartbeat` |
| [`LoginGameZoneZoneMsg.cpp`](LoginServer/LoginGameZoneZoneMsg.cpp) | 1 | 匿名命名空间 `onZoneStatusReport` |
| [`LoginGameZoneRechargeMsg.cpp`](LoginServer/LoginGameZoneRechargeMsg.cpp) | 1 | `LoginRechargeService::onRechargeReq` |
| [`LoginGameZoneGmMsg.cpp`](LoginServer/LoginGameZoneGmMsg.cpp) | 1 | `LoginGmService::onGmCmdReq` |

现有 [`registerInternalRaw`](sdk/util/MsgHandlerBinder.h) 仅支持 **成员函数指针**；Auth/Gateway/Zone 无法直接绑定，需扩展 SDK 或把逻辑搬进 `LoginServer` 成员（后者 diff 更大，不推荐）。

Recharge/Gm 可直接用现有 `registerInternalRaw` 绑定 Service 方法。

---

## SDK 扩展（一步）

在 [`sdk/util/MsgHandlerBinder.h`](sdk/util/MsgHandlerBinder.h) 新增自由函数绑定（与成员版对称）：

```cpp
/** @brief 区内消息：自由函数 (Server&, ConnID, raw body) */
template<typename Server>
void registerInternalFree(MsgDispatcher& d, Server& server, uint16_t msgId,
    void (*fn)(Server&, ConnID, const char*, uint16_t));

/** @brief 区内定长消息：自由函数 + sizeof 守卫 + const BodyT& */
template<typename Server, typename BodyT>
void registerInternalFree(MsgDispatcher& d, Server& server, uint16_t msgId,
    void (*fn)(Server&, ConnID, const BodyT&));
```

实现与 `registerInternal` / `registerInternalRaw` 相同模式：`d.Register` + lambda 内 `static_cast<ConnID>`。

---

## 各文件改造

### 1. Auth — [`LoginGameZoneAuthMsg.cpp`](LoginServer/LoginGameZoneAuthMsg.cpp)

- `#include "../sdk/util/MsgHandlerBinder.h"`
- Handler 签名改为定长引用（去掉手写 `sizeof`）：
  - `onVerifyTokenReq(LoginServer&, ConnID, const Msg_Login_VerifyTokenReq&)`
  - `onUpdateLastUserReq(LoginServer&, ConnID, const Msg_Login_UpdateLastUserReq&)`
- `LoginGameZoneAuthMsgRegister`：

```cpp
registerInternalFree<LoginServer, Msg_Login_VerifyTokenReq>(
    d, server, static_cast<uint16_t>(InternalMsgID::LOGIN_VERIFY_TOKEN_REQ),
    &onVerifyTokenReq);
registerInternalFree<LoginServer, Msg_Login_UpdateLastUserReq>(
    d, server, static_cast<uint16_t>(InternalMsgID::LOGIN_UPDATE_LAST_USER_REQ),
    &onUpdateLastUserReq);
```

### 2. Gateway — [`LoginGameZoneGatewayMsg.cpp`](LoginServer/LoginGameZoneGatewayMsg.cpp)

- 两 handler 均使用 `Msg_Login_GatewayRegister`（心跳与注册同结构，行为不变）
- `registerInternalFree<LoginServer, Msg_Login_GatewayRegister>` × 2（`LOGIN_GATEWAY_REGISTER_REQ` / `LOGIN_GATEWAY_HEARTBEAT`）

### 3. Zone — [`LoginGameZoneZoneMsg.cpp`](LoginServer/LoginGameZoneZoneMsg.cpp)

- `onZoneStatusReport(LoginServer&, ConnID, const Msg_Login_ZoneStatusReport&)`
- `registerInternalFree<LoginServer, Msg_Login_ZoneStatusReport>`（`LOGIN_ZONE_STATUS_REPORT`）

### 4. Recharge — [`LoginGameZoneRechargeMsg.cpp`](LoginServer/LoginGameZoneRechargeMsg.cpp)

```cpp
registerInternalRaw(d, &server.rechargeService(),
    static_cast<uint16_t>(InternalMsgID::LOGIN_RECHARGE_REQ),
    &LoginRechargeService::onRechargeReq);
```

`sizeof` 守卫保留在 [`LoginRechargeService::onRechargeReq`](LoginServer/LoginRechargeService.cpp)（骨架、body 未定型，暂不改签名）。

### 5. GM — [`LoginGameZoneGmMsg.cpp`](LoginServer/LoginGameZoneGmMsg.cpp)

```cpp
registerInternalRaw(d, &server.gmService(),
    static_cast<uint16_t>(InternalMsgID::LOGIN_GM_CMD_REQ),
    &LoginGmService::onGmCmdReq);
```

---

## 不变项

- [`LoginGameZoneMsg.cpp`](LoginServer/LoginGameZoneMsg.cpp) 聚合顺序与 `GameZoneMsgRegisterForwardDispatch()` 调用**不改**
- Handler **业务逻辑**（MySQL、gatewayRegistry、zoneInfoStore）零语义变更
- **不**迁移 [`LoginClientMsgRegister.cpp`](LoginServer/LoginClientMsgRegister.cpp)（客户端口，属另一 Dispatcher）

---

## 验证

```bash
cd .build && cmake .. && make -j4 LoginServer
```

- grep 确认 `LoginServer/LoginGameZone*.cpp` 无残留 `d.Register(`
- 可选手工：Super 转发 `LOGIN_VERIFY_TOKEN_REQ`、Gateway `LOGIN_GATEWAY_REGISTER`、区状态 `LOGIN_ZONE_STATUS_REPORT` 路径日志正常

---

## 预期结果

- LoginGameZone 7 处 lambda 注册 → 7 行 binder 调用
- Auth/Gateway/Zone 去掉 5 处重复 `if (len < sizeof(...)) return`
- SDK 获得可复用的 `registerInternalFree`，后续 SuperLoginMsg 等自由函数模块可同样迁移
