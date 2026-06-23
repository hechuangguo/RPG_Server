# Unity 客户端 — 登录 / 选角 / 进世界契约

本文档面向 Unity（或其它客户端）实现方，与 [`scripts/test_login_gateway_e2e.py`](../scripts/test_login_gateway_e2e.py) 及服务端行为对齐。**鉴权成功后必须保持 Gateway 连接**，直至主动退出或回登录。

---

## 1. 两阶段连接

| 阶段 | 地址 | 生命周期 |
|------|------|----------|
| Phase A | LoginServer `9010`（TLS） | 收 token + `S2C_GATEWAY_INFO` 后**断开** |
| Phase B | GatewayServer `9005`（TLS） | 鉴权成功后**保持连接**直到离世界/断线 |

---

## 2. LoginServer（9010）

1. TLS 连接建立后，首包为 **`S2C_LOGIN_CHALLENGE`**（`module=0x00 sub=0x10`），`nonce` 16 字节。
2. `C2S_LOGIN_REQ` / `C2S_REGISTER_REQ`：
   - `password_digest` = **SHA-256(UTF-8 明文密码)**，32 字节（**不是** `SHA256(nonce||password)`）。
   - `login_nonce` = 原样回显 challenge 的 `nonce`。
3. 成功：`S2C_LOGIN_RSP`（`code=0`）+ `S2C_GATEWAY_INFO`（ip/port）→ **断开 9010**。
4. 失败：读 `S2C_LOGIN_RSP.msg`，可重试（每次需新 challenge；断线重连会下发新 nonce）。

---

## 3. GatewayServer（9005）— 必须遵守

### 3.1 鉴权

- 发 `C2S_GATEWAY_AUTH_REQ`（`login_token`、`zone_id`、`game_type` 与登录区一致）。
- 等待 **`S2C_GATEWAY_AUTH_RSP`**（sub=0x11，`code=0`）表示账号鉴权成功，状态进入 **ACCOUNT_OK**。
- Super→Login 外联偶发重试时，**最多等待 17s**（`GATEWAY_AUTHING_TIMEOUT_MS`），期间仅发心跳，勿断开。

### 3.2 角色列表（关键）

- 鉴权成功后服务端**主动推送** `S2C_USER_LIST`，客户端**无需**发列表请求。
- **`count=0` 是正常**：表示无角色，应显示创角 UI，**禁止**因空列表 `Disconnect`。
- `code!=0`：提示错误，可重连或等待重试，勿假定已断线。

### 3.3 创角

- 仅在 `ACCOUNT_OK` 发 `C2S_CREATE_USER_REQ`。
- 收 `S2C_CREATE_USER_RSP`：
  - `code=0`：成功；随后会再收刷新后的 `S2C_USER_LIST`。
  - `code=1`（角色名已存在）：**仅提示换名，保持 Gateway 连接**。
  - 其它非 0：提示后保持连接，允许重试。
- **禁止**在创角失败时调用 `Disconnect` 或回到 Login 重连（除非用户主动取消）。

### 3.4 选角进世界

- `C2S_SELECT_USER_REQ`：`user_id` 必须属于当前 `S2C_USER_LIST` 或创角返回的 `user_id`。
- 成功后可能先收 `S2C_SPAWN_ENTITY`（邻居/NPC），再收 **`S2C_ENTER_WORLD_RSP`**（0x12）+ **`S2C_ENTER_GAME`**（0x09）；客户端须缓冲 AOI 包。
- 进世界前状态为 `ENTERING`：仅心跳，勿重复发选角/创角。

### 3.5 常见错误（日志对照）

| 客户端现象 | 可能原因 | 处理 |
|-----------|----------|------|
| 鉴权后立刻断 Gateway | 收到空列表或创角失败后主动断开 | 保持连接，见 §3.2–3.3 |
| `state=2 ACCOUNT_OK` 后无 UI | 未解析 `S2C_USER_LIST` Protobuf | 按 `rpg.login.S2CUserList` 解析 |
| 创角「名已存在」后无法选角 | 库中已有角色，应直接选角 | 用列表中 `user_id` 发 `SELECT_USER` |
| 17s 鉴权超时 | 外联 TLS 闪断 + 重排队 | 重试登录；服务端已对齐超时预算 |

---

## 4. 参考实现

- 通过标准：[`scripts/test_login_gateway_e2e.py`](../scripts/test_login_gateway_e2e.py)
- 流程说明：[`docs/LOGIN_CHAR_FLOW.md`](LOGIN_CHAR_FLOW.md)
- 协议索引：[`docs/PROTOCOL.md`](PROTOCOL.md)

---

## 5. Unity 检查清单

- [ ] 9010：解析 `S2C_LOGIN_CHALLENGE`，密码摘要算法正确
- [ ] 9005：等待 `S2C_GATEWAY_AUTH_RSP`（0x11），鉴权成功后**不**断开
- [ ] `S2C_USER_LIST count=0` → 创角界面
- [ ] `S2C_CREATE_USER_RSP code!=0` → 提示，**不断开**
- [ ] 列表 `count>=1` → 选角 UI，发 `C2S_SELECT_USER_REQ`
- [ ] 缓冲 `S2C_SPAWN_ENTITY` 直至 `S2C_ENTER_WORLD_RSP` + `S2C_ENTER_GAME`
- [ ] AUTHING 等待 ≤17s，期间发心跳
