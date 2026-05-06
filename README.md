# muduo-distributed-chat-server

基于 muduo 的分布式可靠消息服务。项目支持注册登录、单聊、群聊、好友/群组管理、离线消息、跨节点转发、消息 ACK、断线重连后的未读同步，并可通过 Nginx Stream 进行 TCP 负载均衡。

## 技术栈

- C++11
- muduo：多 Reactor TCP 长连接网络层
- MySQL / MariaDB：用户、好友、群组、可靠消息持久化
- Redis Pub/Sub：跨节点消息转发
- Nginx Stream：TCP 负载均衡
- nlohmann/json：应用层消息序列化

## 架构

```text
client
  |
  | TCP JSON frame: json + '\n'
  v
Nginx Stream :8000
  |
  +---- ChatServer-1 :6000 ---- MySQL
  |          |                  Redis Pub/Sub
  +---- ChatServer-2 :6002 ---- MySQL
```

用户登录到某个 ChatServer 后，该节点保存 `userid -> TcpConnection`，并订阅 Redis 中该用户 id 对应的 channel。若 A 和 B 在不同节点，A 所在节点无法在本机连接表中找到 B，但查询到 B 为 online 后，会将消息发布到 B 的 Redis channel，B 所在节点收到订阅消息后再投递给 B。

## 这版相对原始项目的升级点

1. **应用层拆包协议**：客户端和服务端统一使用 `json.dump() + '\n'` 作为一帧，服务端在 muduo Buffer 中循环拆帧，避免粘包/半包导致 JSON 解析失败。
2. **可靠消息表**：新增 `chat_message` 表，记录 `message_id / sender_id / receiver_id / group_id / status / payload`。
3. **ACK 确认**：客户端收到单聊/群聊消息后自动发送 `MSG_ACK`，服务端将消息状态更新为 `acked`。
4. **断线重连未读同步**：登录时查询 `status < acked` 的消息，下发给客户端并更新为 `delivered`。
5. **群聊按接收人追踪**：群消息会为每个接收者生成独立 `message_id`，便于按人 ACK、按人重发。
6. **配置外置**：MySQL 配置支持环境变量，不再只依赖代码里的硬编码账号密码。
7. **压测脚本**：新增 `benchmark/bench_client.py`，用于模拟多客户端登录、发送消息、自动 ACK 和统计延迟。

## 消息状态

```text
0 created    服务端已持久化，尚未投递到接收者连接
1 delivered  服务端已投递到接收者连接，但客户端尚未ACK
2 acked      客户端已确认收到
```

## 快速启动

### 1. 安装依赖

Alibaba Cloud Linux / CentOS 系可以参考：

```bash
yum install -y gcc gcc-c++ cmake make mysql-devel hiredis-devel nginx redis mariadb-server
```

muduo 需要单独安装到系统库路径，确保可以链接 `muduo_net` 和 `muduo_base`。

### 2. 初始化数据库

```bash
mysql -uroot -p < sql/init.sql
```

### 3. 配置数据库连接

```bash
cp config/server.env.example config/server.env
vim config/server.env
source config/server.env
```

### 4. 编译

```bash
./scripts/build.sh
```

### 5. 启动 Redis / MySQL

```bash
systemctl start redis
systemctl start mariadb
```

### 6. 启动两个聊天节点

```bash
./scripts/run_server.sh 127.0.0.1 6000
./scripts/run_server.sh 127.0.0.1 6002
```

### 7. Nginx TCP 负载均衡

把 `config/nginx_tcp.conf` 合并到 nginx 配置后 reload：

```bash
nginx -t
nginx -s reload
```

客户端连接 Nginx：

```bash
./bin/ChatClient 127.0.0.1 8000
```

## 压测

先准备一批测试用户，然后运行压测：

```bash
python3 benchmark/bench_client.py --host 127.0.0.1 --port 8000 --mode register --users 100 --password 123456
python3 benchmark/bench_client.py --host 127.0.0.1 --port 8000 --mode chat --start-id 1 --users 100 --password 123456 --messages-per-user 100 --concurrency 100
```

脚本会输出连接成功数、发送消息数、接收消息数、ACK 数、平均延迟、P95/P99 延迟等指标。更详细的压测方案见 `docs/perf-test.md`。

## 仓库建议

这个项目建议作为简历主项目之一，仓库名可以使用：

```text
muduo-distributed-chat-server
```

GitHub 上建议补充架构图、压测截图、部署截图，并在 README 中放一份真实压测结果。
