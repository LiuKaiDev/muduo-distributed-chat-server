# 架构说明

## 模块划分

```text
src/server/chatserver.cpp     网络层：连接回调、读写事件、JSON拆帧
src/server/chatservice.cpp    业务层：登录、注册、单聊、群聊、ACK、心跳、跨节点转发
src/server/model/*            数据层：User/Friend/Group/Message ORM
src/server/redis/*            Redis Pub/Sub 封装
src/client/main.cpp           交互式客户端，支持自动ACK
benchmark/bench_client.py     异步压测客户端
config/server.env.example     MySQL / Redis 环境变量配置示例
```

## 单聊投递流程

```text
1. client A 发送 ONE_CHAT_MSG
2. server 为消息生成 message_id
3. 写入 chat_message，status=created
4. 如果 B 在本机连接表：直接投递，status=delivered
5. 如果 B 在线但在其他节点：publish 到 Redis channel=B
6. B 所在节点收到订阅消息并投递，status=delivered
7. B 客户端收到消息后发送 MSG_ACK
8. server 更新 chat_message.status=acked
```

如果 Redis 暂时不可用或 publish 失败，消息不会被标记为 delivered，仍保留在 `chat_message` 中，等待接收方下一次登录/重连时同步。

## 断线重连同步

```text
1. 用户重新登录
2. server 查询 chat_message where receiver_id = 当前用户 and status < acked
3. 将消息放入 LOGIN_MSG_ACK.offlinemsg 下发
4. 下发后标记 delivered
5. 客户端展示消息并发送 ACK
```

## 为什么群聊要每个接收者单独生成 message_id

群聊中不同用户可能处在不同状态：有人在线并 ACK，有人离线，有人在另一个节点。如果一条群消息只有一个 message_id，就无法准确表示每个接收者的投递状态。所以服务端会为每个群成员生成一条 chat_message 记录，分别追踪状态。

## 心跳保活

客户端登录后定期发送 `HEARTBEAT_MSG`。服务端不依赖心跳改变业务状态，只返回当前服务端时间，用于维持 TCP 长连接活跃，降低 Nginx Stream 或中间网络设备因空闲超时断开连接的概率。
