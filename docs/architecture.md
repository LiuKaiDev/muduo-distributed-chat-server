# 架构说明

本文档用于说明聊天服务的模块划分、消息投递流程、跨节点转发机制以及可靠消息设计。

## 模块划分

```text
src/server/chatserver.cpp
网络层，基于 muduo TcpServer 封装连接建立、连接断开、读写事件回调和应用层拆包逻辑。

src/server/chatservice.cpp
业务层，负责登录、注册、单聊、群聊、添加好友、创建群组、ACK、心跳和跨节点转发等核心业务逻辑。

src/server/model/
数据访问层，封装用户、好友、群组、离线消息和可靠消息表的数据库操作。

src/server/db/
MySQL / MariaDB 连接封装，负责数据库连接初始化、SQL 执行和资源释放。

src/server/redis/
Redis Pub/Sub 封装，负责跨 ChatServer 节点的消息发布、订阅和回调分发。

src/client/main.cpp
交互式客户端，支持注册、登录、单聊、群聊、好友管理、群组管理和消息 ACK。
```


## 单聊消息投递流程

```text
1. 客户端 A 发送 ONE_CHAT_MSG。
2. ChatServer 解析请求，生成 message_id。
3. 服务端将消息写入 chat_message 表，初始状态为 created。
4. 如果用户 B 在线且连接在当前节点，服务端直接通过本地 TcpConnection 投递消息，并将状态更新为 delivered。
5. 如果用户 B 在线但连接在其他 ChatServer 节点，当前节点将消息 publish 到 Redis 中以用户 B 为标识的 channel。
6. 用户 B 所在节点收到 Redis 订阅消息后，将消息投递到本地 TcpConnection，并更新消息状态。
7. 客户端 B 收到消息后发送 MSG_ACK。
8. 服务端收到 ACK 后，将 chat_message.status 更新为 acked。
```

如果 Redis 暂时不可用，或者跨节点 publish 失败，消息不会被视为已完成投递，仍保留在 `chat_message` 表中，等待接收方重新登录或重连后进行未读消息同步。

## 断线重连与未读消息同步

用户断线或离线后，未完成 ACK 的消息会保留在数据库中。用户重新登录时，服务端会执行未读消息同步流程：

```text
1. 用户重新登录。
2. 服务端查询 chat_message 中 receiver_id 为当前用户且 status < acked 的消息。
3. 服务端将这些消息放入登录响应中的 offlinemsg 字段下发给客户端。
4. 下发后，服务端将消息状态更新为 delivered。
5. 客户端展示消息后发送 MSG_ACK。
6. 服务端收到 ACK 后，将消息状态更新为 acked。
```

该流程用于保证用户断线或离线期间的消息不会直接丢失，并支持重连后的未读消息恢复。

## 群聊消息设计

群聊场景下，不同成员可能处于不同状态：

```text
成员 A 在线，并且已经 ACK
成员 B 在线，但连接在其他节点
成员 C 离线
成员 D 断线后重新登录
```

如果一条群消息只使用一个统一的 `message_id`，就无法准确描述每个接收者的投递状态。

因此，服务端会为每个群成员生成独立的 `chat_message` 记录：

```text
同一条群聊内容
        |
        +-- receiver_id = 用户 A，message_id = 1001
        +-- receiver_id = 用户 B，message_id = 1002
        +-- receiver_id = 用户 C，message_id = 1003
```

这样可以按用户维度分别追踪消息的 `created`、`delivered` 和 `acked` 状态，便于支持离线同步、重复投递控制和 ACK 确认。

## Redis 跨节点转发

每个用户登录成功后，所在 ChatServer 会维护本地连接映射：

```text
userid -> TcpConnection
```

同时订阅 Redis 中以该用户 ID 为标识的 channel。

当消息接收方不在当前节点时，当前节点通过 Redis 发布消息：

```text
ChatServer-1 publish message to channel userB
Redis Pub/Sub
ChatServer-2 receive message
ChatServer-2 deliver message to userB connection
```

Redis Pub/Sub 只负责节点间消息通知和转发，消息状态仍然以 MySQL / MariaDB 中的 `chat_message` 表为准。

## 心跳保活

客户端登录成功后，会定期发送 `HEARTBEAT_MSG`。

服务端收到心跳后返回心跳响应。心跳主要用于：

```text
维持 TCP 长连接活跃
降低 Nginx Stream 或中间网络设备因连接长时间空闲而断开的概率
辅助判断客户端连接是否仍然可用
```

当前心跳机制不直接改变用户业务状态，用户状态仍主要由登录、注销、连接断开等事件维护。

## 消息状态

`chat_message` 表中的消息状态定义如下：

```text
0 created    服务端已持久化，尚未投递到接收方连接
1 delivered  服务端已投递到接收方连接，等待客户端 ACK
2 acked      客户端已确认收到
```

状态流转：

```text
created -> delivered -> acked
```

该状态机用于支持消息投递跟踪、ACK 确认和断线重连后的未读消息同步。
