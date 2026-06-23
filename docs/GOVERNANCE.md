# RPG Server 工程治理

长期发布、灰度、回滚与安全审计流程，配合三维度改造计划使用。

## 变更门禁

每次涉及稳定性/安全性/扩展性的改动须具备：

| 项 | 说明 |
|----|------|
| **可观测** | 登录链路 SLI、发送失败、重连、速率限制命中等可通过日志或 `ServiceHealthMetrics` 查看 |
| **灰度开关** | 新行为优先用配置/环境变量（如 `RPG_PRODUCTION=1`）控制 |
| **回滚路径** | 保留上一版二进制与 `config.xml`；协议双栈期须兼容旧客户端 |
| **压测/验证** | 登录 E2E：`TLS_INSECURE=1 python3 scripts/test_login_gateway_e2e.py <账号> <密码>` |

## 发布策略

1. **单服灰度**：先升级 Super → Record/Login → Gateway → Session → Scene/AOI
2. **按 serverId 扩容**：新 Scene/AOI 实例写入 `ServerList` 后由 Super 增量刷新拓扑
3. **协议变更**：`Common/*.proto` 变更后运行 `./scripts/gen_proto.sh`，保留至少一个版本的双栈兼容期

## 回滚

1. `./StopServer.sh` 停止全区进程
2. 恢复上一版 `.build/bin/*` 与 `config/config.xml`
3. `./RunServer.sh` 与 `./RunServer.sh login` 重启
4. 核对 `logs/*.log` 中登录成功率与重连风暴是否恢复

## 生产配置校验

```bash
RPG_PRODUCTION=1 ./scripts/validate_production_config.sh
```

要求：`TLS enabled=1`、`verifyPeer=1`，且数据库密码非默认/空。

## 安全审计清单（季度）

- [ ] `RPG_PRODUCTION=1` 下全进程启动通过 `validateProductionConfig`
- [ ] Gateway `ClientMsgValidator` 与 Login `LoginClientMsgValidator` 白名单覆盖所有客户端 sub
- [ ] 区内 mTLS 证书非全进程共用；`verifyPeer=1`
- [ ] Scene Lua 沙箱未开放 `io`/`os`/`debug`
- [ ] 依赖与静态扫描（SAST）结果已归档
- [ ] 登录/鉴权速率限制与超时预算与 `LoginFlowTimeouts.h` 一致

## 验收指标（阶段对照）

| 维度 | 目标 |
|------|------|
| 稳定性 | 登录成功率↑、超时率↓、handler 内无同步长轮询 |
| 安全性 | P0/P1 高风险项闭环、生产误配率↓ |
| 扩展性 | 新 Scene/AOI 实例可热接入；`playerCount` 驱动选服 |
