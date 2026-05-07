我查了一下比较通用的 README 写法：GitHub 官方建议 README 重点回答“项目做什么、为什么有用、如何开始使用、哪里看更多文档”等问题；同时 README 通常是访问仓库时最先看到的内容，所以开头要简洁、可信、不要写太多给自己看的内容。([GitHub Docs][1]) 另外，GitHub 文档也建议 README 只放开发者开始使用项目所需的信息，更长的内容放到 `docs/` 里会更合适。([GitHub][2])

你现在这版 README 最大的问题是：**“简历项目”“压测”“高并发”说得太明显、太满**。我建议主 README 改成下面这种风格：项目定位清楚、技术点充分、但不夸大性能。

你可以直接把下面这一版替换你的 `README.md` 主体内容：

````markdown
# muduo-distributed-chat-server

基于 muduo 实现的 C++ 分布式聊天服务，采用网络层、业务层、数据层解耦设计，支持注册登录、好友管理、单聊、群聊、离线消息持久化、跨节点消息转发和断线重连后的未读消息同步。

项目基于 MySQL / MariaDB 持久化用户、好友、群组和消息状态，基于 Redis Pub/Sub 实现多 ChatServer 节点之间的消息路由，并支持通过 Nginx Stream 对 TCP 长连接进行四层负载均衡。

## 技术栈

| 模块 | 技术 |
|---|---|
| 开发语言 | C++11 |
| 网络库 | muduo |
| 通信协议 | TCP + JSON |
| 数据存储 | MySQL / MariaDB |
| 跨节点通信 | Redis Pub/Sub |
| 负载均衡 | Nginx Stream |
| 构建工具 | CMake |
| JSON 库 | nlohmann/json |

## 核心功能

- 注册、登录、注销
- 好友添加与好友列表查询
- 单聊消息发送与接收
- 群组创建、加入与群聊
- 离线消息持久化与登录后同步
- 基于 Redis Pub/Sub 的跨节点消息转发
- 基于 Nginx Stream 的 TCP 长连接负载均衡
- 基于 message_id、ACK 和消息状态的消息投递跟踪
- 心跳保活，降低长连接空闲断开的概率
- MySQL、Redis 连接信息配置化，避免敏感信息写入代码

## 系统架构

```text
                     +----------------+
                     |     Client     |
                     +--------+-------+
                              |
                              | TCP / JSON
                              v
                     +----------------+
                     | Nginx Stream   |
                     |   listen 8000  |
                     +--------+-------+
                              |
              +---------------+---------------+
              |                               |
              v                               v
+----------------------------+   +----------------------------+
| ChatServer-1               |   | ChatServer-2               |
| 127.0.0.1:6000             |   | 127.0.0.1:6002             |
|                            |   |                            |
| userid -> TcpConnection    |   | userid -> TcpConnection    |
+-------------+--------------+   +-------------+--------------+
              |                                |
              +---------------+----------------+
                              |
          +-------------------+-------------------+
          |                                       |
          v                                       v
+-------------------+                 +-------------------+
| MySQL / MariaDB   |                 | Redis Pub/Sub     |
| 用户/好友/群组/消息 |                 | 跨节点消息转发      |
+-------------------+                 +-------------------+
````

## 模块设计

```text
src/
├── client/                 # 客户端入口与命令处理
└── server/
    ├── chatserver.cpp      # muduo TcpServer 封装
    ├── chatservice.cpp     # 业务分发与核心业务逻辑
    ├── db/                 # MySQL 连接封装
    ├── model/              # 用户、好友、群组、消息数据模型
    └── redis/              # Redis 发布订阅封装

include/
└── server/                 # 服务端头文件

config/
├── server.env.example      # 服务端环境变量模板
└── nginx_tcp.conf          # Nginx Stream 配置示例

sql/
└── init.sql                # 数据库初始化脚本

docs/
├── architecture.md         # 架构说明
├── protocol.md             # 应用层协议说明
└── perf-test.md            # 基础测试说明

benchmark/
└── bench_client.py         # 多客户端基础测试脚本
```

## 消息投递流程

### 单节点在线消息

```text
User A Client
      |
      | chat: userB
      v
ChatServer
      |
      | 查询 User B 是否在线
      v
User B TcpConnection
      |
      | 客户端收到消息后返回 ACK
      v
ChatServer 更新消息状态
```

### 跨节点在线消息

```text
User A -> ChatServer-1
              |
              | User B 不在本机连接表
              | publish 到 Redis channel
              v
          Redis Pub/Sub
              |
              v
User B <- ChatServer-2
```

用户登录到某个 ChatServer 后，该节点维护 `userid -> TcpConnection` 的本地连接路由，并订阅 Redis 中以 `userid` 命名的 channel。当发送方与接收方不在同一节点时，发送方所在节点通过 Redis 发布消息，接收方所在节点收到订阅消息后投递到本地 TCP 连接。

## 可靠消息设计

项目引入 `chat_message` 表记录消息投递状态，用于支持离线消息、ACK 确认和断线重连后的未读消息同步。

消息状态定义：

```text
0 created    服务端已持久化，尚未投递到接收方连接
1 delivered  服务端已投递到接收方连接，等待客户端 ACK
2 acked      客户端已确认收到
```

核心流程：

```text
1. 服务端收到聊天请求后生成 message_id
2. 消息写入 chat_message 表，状态为 created
3. 如果接收方在线，服务端立即投递消息并更新为 delivered
4. 客户端收到消息后发送 MSG_ACK
5. 服务端收到 ACK 后将消息状态更新为 acked
6. 如果接收方离线，消息保留在数据库中
7. 接收方重新登录后，服务端查询未确认消息并同步给客户端
```

群聊消息按接收用户维度生成独立消息记录，便于分别追踪每个用户的投递状态和 ACK 状态。

## 应用层协议

客户端和服务端使用 JSON 作为应用层消息格式。当前实现中，每条消息以：

```text
json.dump() + '\n'
```

作为一帧，服务端按换行符进行应用层拆包处理，避免 TCP 粘包和半包导致的 JSON 解析问题。

示例登录请求：

```json
{
  "msgid": 1,
  "id": 1,
  "password": "123456"
}
```

示例单聊请求：

```json
{
  "msgid": 5,
  "id": 1,
  "to": 2,
  "msg": "hello"
}
```

更多协议说明见：[docs/protocol.md](docs/protocol.md)

## 快速启动

### 1. 安装依赖

Alibaba Cloud Linux / CentOS 可参考：

```bash
yum install -y gcc gcc-c++ cmake make mysql-devel hiredis-devel nginx redis mariadb-server
```

muduo 需要单独安装，并确保系统可以链接：

```text
muduo_net
muduo_base
```

### 2. 启动 MySQL 和 Redis

```bash
systemctl enable --now mariadb
systemctl enable --now redis
redis-cli ping
```

Redis 正常时应返回：

```text
PONG
```

### 3. 初始化数据库

```bash
mysql -uroot -p < sql/init.sql
```

如果本地 root 用户无密码，也可以使用：

```bash
mysql -uroot < sql/init.sql
```

### 4. 配置服务端环境变量

```bash
cp config/server.env.example config/server.env
vim config/server.env
source config/server.env
```

配置示例：

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

注意：`config/server.env` 包含本地数据库密码，不建议提交到 GitHub。仓库中只保留 `config/server.env.example`。

### 5. 编译项目

```bash
./scripts/build.sh
```

编译完成后会生成：

```text
bin/ChatServer
bin/ChatClient
```

### 6. 启动单节点服务

```bash
source config/server.env
./bin/ChatServer 127.0.0.1 6000
```

新开终端启动客户端：

```bash
./bin/ChatClient 127.0.0.1 6000
```

客户端启动后可以进行注册、登录、添加好友、单聊和群聊测试。

## 多节点部署

启动两个 ChatServer 节点：

```bash
source config/server.env
./bin/ChatServer 127.0.0.1 6000
```

```bash
source config/server.env
./bin/ChatServer 127.0.0.1 6002
```

将 `config/nginx_tcp.conf` 合并到 Nginx 配置中，检查并重载：

```bash
nginx -t
nginx -s reload
```

客户端连接 Nginx 入口：

```bash
./bin/ChatClient 127.0.0.1 8000
```

Nginx 负责将不同客户端连接分发到多个 ChatServer 节点，Redis Pub/Sub 负责不同节点之间的消息转发。

## 基础测试

项目提供 `benchmark/bench_client.py`，用于模拟多客户端连接、登录和单聊消息收发，主要用于功能验证和稳定性观察。

注册测试用户：

```bash
python3 benchmark/bench_client.py \
  --host 127.0.0.1 \
  --port 6000 \
  --mode register \
  --users 100 \
  --password 123456 \
  --concurrency 50
```

单聊链路测试：

```bash
python3 benchmark/bench_client.py \
  --host 127.0.0.1 \
  --port 6000 \
  --mode chat \
  --start-id 1 \
  --users 100 \
  --password 123456 \
  --messages-per-user 20 \
  --concurrency 50
```

该脚本主要用于验证连接、登录、消息收发和 ACK 流程，不作为极限性能测试结果。

## 项目亮点

* 基于 muduo 多 Reactor 模型实现 TCP 长连接服务，网络层与业务层通过消息类型分发解耦。
* 设计 JSON 应用层协议，并在服务端进行应用层拆包处理，解决 TCP 粘包和半包问题。
* 基于 MySQL / MariaDB 持久化用户、好友、群组和消息状态，支持离线消息恢复。
* 基于 Redis Pub/Sub 实现跨节点消息路由，支持用户连接分布在不同 ChatServer 节点时的消息转发。
* 引入 message_id、ACK 和消息状态字段，支持消息投递跟踪和断线重连后的未读消息同步。
* 支持通过 Nginx Stream 对多个 ChatServer 节点进行 TCP 长连接负载均衡。
* 使用环境变量管理数据库和 Redis 配置，避免将敏感信息硬编码到代码中。

## 后续优化方向

* 将当前 `JSON + '\n'` 协议升级为 `固定长度消息头 + JSON Body`，进一步增强协议通用性。
* 优化客户端输入校验，避免非法输入导致客户端进入异常状态。
* 增加 Docker Compose，一键启动 MySQL、Redis、Nginx 和 ChatServer。
* 优化可靠消息投递链路，支持 ACK 批量更新和异常重试。
* 补充多节点部署验证文档和更完整的功能测试用例。
* 增加 CI 构建检查，保证代码提交后能够自动编译验证。

## 文档

* [架构说明](docs/architecture.md)
* [应用层协议](docs/protocol.md)
* [基础测试说明](docs/perf-test.md)

## License

This project is for learning and demonstration purposes.

```

这版我主要帮你改了三点：

第一，删掉了“这个仓库适合作为简历项目”这种不适合公开展示的话。第二，把“压测”改成了“基础测试”，避免被面试官抓住性能结果追问。第三，把重点放到了 muduo、Redis、MySQL、可靠消息、Nginx 这些真实工程点上。
::contentReference[oaicite:2]{index=2}
```

[1]: https://docs.github.com/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes?utm_source=chatgpt.com "About the repository README file - GitHub Docs"
[2]: https://github.com/github/docs/blob/main/content/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes.md?utm_source=chatgpt.com "docs/content/repositories/managing-your-repositorys-settings ... - GitHub"
