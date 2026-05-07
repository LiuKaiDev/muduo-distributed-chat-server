# muduo-distributed-chat-server

基于 muduo 的分布式可靠消息服务，支持注册登录、单聊、群聊、好友/群组管理、离线消息、跨节点转发、消息 ACK、断线重连后的未读同步，并可通过 Nginx Stream 做 TCP 负载均衡。

这个仓库适合作为 C++ 后端/网络编程方向的简历项目：网络层、业务层、数据层解耦清晰，可靠消息链路在代码、SQL、协议和压测脚本中都有对应实现。

## 技术栈

- C++11
- muduo：多 Reactor TCP 长连接网络层
- MySQL / MariaDB：用户、好友、群组、可靠消息持久化
- Redis Pub/Sub：跨 ChatServer 节点消息转发
- Nginx Stream：TCP 四层负载均衡
- nlohmann/json：应用层 JSON 协议

## 架构

```text
Client
  |
  | TCP JSON frame: json + '\n'
  v
Nginx Stream :8000
  |
  +---- ChatServer-1 :6000 ---- MySQL
  |          |                  Redis Pub/Sub
  +---- ChatServer-2 :6002 ---- MySQL
```

用户登录到某个 ChatServer 后，该节点维护 `userid -> TcpConnection` 的本地路由，并订阅 Redis 中以 `userid` 命名的 channel。A 给 B 发送消息时，如果 B 不在本机连接表但数据库状态为在线，服务端会将消息 publish 到 B 的 Redis channel，B 所在节点收到订阅消息后再投递到本地 TCP 连接。

## 核心能力

1. **多 Reactor 长连接网络层**：基于 muduo `TcpServer` 处理连接接入、读写事件与消息分发。
2. **应用层拆包协议**：客户端和服务端统一使用 `json.dump() + '\n'` 作为一帧，解决 TCP 粘包/半包问题。
3. **可靠消息表**：`chat_message` 记录 `message_id / sender_id / receiver_id / group_id / status / payload`。
4. **ACK 与投递状态**：客户端收到单聊/群聊消息后自动发送 `MSG_ACK`，服务端将消息状态更新为 `acked`。
5. **断线重连未读同步**：登录时查询 `status < acked` 的消息，下发后标记为 `delivered`，客户端展示后继续 ACK。
6. **群聊按人追踪**：群消息为每个接收者生成独立 `message_id`，便于按人投递、按人 ACK、按人重发。
7. **跨节点转发降级**：Redis 不可用时不会拖垮登录/单节点聊天；跨节点 publish 失败的消息仍保留在可靠消息表中，等待接收方重连同步。
8. **心跳保活**：客户端登录后定期发送 `HEARTBEAT_MSG`，服务端返回心跳响应，降低长连接被负载均衡或中间设备空闲断开的概率。
9. **配置化管理**：MySQL、Redis 连接信息通过环境变量配置，避免把真实密码提交到 GitHub。
10. **压测脚本**：`benchmark/bench_client.py` 支持批量注册、并发登录、发送消息、自动 ACK 和延迟统计。

## 消息状态

```text
0 created    服务端已持久化，尚未投递到接收者连接
1 delivered  服务端已投递到接收者连接，但客户端尚未 ACK
2 acked      客户端已确认收到
```

## 快速启动

### 1. 安装依赖

Alibaba Cloud Linux / CentOS 可参考：

```bash
yum install -y gcc gcc-c++ cmake make mysql-devel hiredis-devel nginx redis mariadb-server
```

muduo 需要单独安装到系统库路径，确保可以链接 `muduo_net` 和 `muduo_base`。

### 2. 初始化数据库

```bash
mysql -uroot -p < sql/init.sql
```

### 3. 配置服务

```bash
cp config/server.env.example config/server.env
vim config/server.env
source config/server.env
```

支持的环境变量：

```bash
CHAT_DB_HOST=127.0.0.1
CHAT_DB_PORT=3306
CHAT_DB_USER=root
CHAT_DB_PASSWORD=123456
CHAT_DB_NAME=chat

CHAT_REDIS_HOST=127.0.0.1
CHAT_REDIS_PORT=6379
CHAT_REDIS_PASSWORD=
CHAT_REDIS_DB=0
```

### 4. 编译

```bash
./scripts/build.sh
```

### 5. 启动依赖服务

```bash
systemctl start redis
systemctl start mariadb
```

### 6. 启动聊天节点

```bash
./scripts/run_server.sh 127.0.0.1 6000
./scripts/run_server.sh 127.0.0.1 6002
```

### 7. Nginx TCP 负载均衡

将 `config/nginx_tcp.conf` 合并到 nginx 配置后 reload：

```bash
nginx -t
nginx -s reload
```

客户端连接 Nginx：

```bash
./bin/ChatClient 127.0.0.1 8000
```

## 压测

先准备一批测试用户：

```bash
python3 benchmark/bench_client.py --host 127.0.0.1 --port 8000 --mode register --users 100 --password 123456
```

再进行聊天链路压测：

```bash
python3 benchmark/bench_client.py --host 127.0.0.1 --port 8000 --mode chat --start-id 1 --users 100 --password 123456 --messages-per-user 100 --concurrency 100
```

脚本会输出连接成功数、发送消息数、接收消息数、ACK 数、平均延迟、P95/P99 延迟等指标。真实压测结果建议记录在 `docs/perf-test.md`，不要在 README 或简历里写未经验证的吞吐数字。

## 文档

- [架构说明](docs/architecture.md)
- [应用层协议](docs/protocol.md)
- [压测方案](docs/perf-test.md)

## 简历描述参考

基于 muduo 多 Reactor 模型实现分布式可靠消息服务，支持注册登录、单聊/群聊、离线消息、跨节点转发与断线重连后的未读同步。系统采用网络层、业务层、数据层解耦设计，结合 Redis Pub/Sub、MySQL 持久化和 Nginx TCP 负载均衡实现高并发长连接通信；设计消息 ID、ACK 确认和消息状态机制，实现消息去重、投递跟踪与按用户维度的可靠消息同步。
