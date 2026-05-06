# 压测方案

## 目标

验证聊天服务在多 TCP 长连接下的基本吞吐和可靠消息链路：

- 并发连接数
- 消息发送数
- 接收消息数
- ACK 数
- 平均延迟 / P95 / P99 延迟
- 失败连接数、登录失败数、发送失败数

## 测试前准备

1. 初始化数据库：`mysql -uroot -p < sql/init.sql`
2. 启动 MySQL、Redis
3. 编译并启动至少一个 ChatServer
4. 可选：启动两个 ChatServer，并通过 Nginx Stream 暴露统一端口 8000

## 注册测试用户

```bash
python3 benchmark/bench_client.py \
  --host 127.0.0.1 \
  --port 8000 \
  --mode register \
  --users 1000 \
  --password 123456
```

记录注册出来的用户 id 区间。若数据库为空，通常从 1 开始。

## 单节点压测

```bash
python3 benchmark/bench_client.py \
  --host 127.0.0.1 \
  --port 6000 \
  --mode chat \
  --start-id 1 \
  --users 500 \
  --password 123456 \
  --messages-per-user 100 \
  --concurrency 500
```

## 多节点 + Nginx 压测

```bash
./scripts/run_server.sh 127.0.0.1 6000
./scripts/run_server.sh 127.0.0.1 6002
# nginx stream listen 8000
python3 benchmark/bench_client.py --host 127.0.0.1 --port 8000 --mode chat --start-id 1 --users 500 --password 123456 --messages-per-user 100 --concurrency 500
```

## 结果记录模板

```text
测试环境：阿里云 2C2G / Alibaba Cloud Linux 3
服务节点：2 个 ChatServer + 1 个 Redis + 1 个 MySQL + Nginx Stream
并发连接数：
总发送消息数：
总接收消息数：
ACK确认数：
平均延迟：
P95延迟：
P99延迟：
CPU：
内存：
结论：
```

## 注意事项

- 真实数据跑出来前，不要把吞吐量和延迟写死到简历或 README。
- 云服务器压测前先检查 `ulimit -n`，必要时提升文件描述符限制。
- 如果出现登录失败，先确认用户是否已经在线，必要时执行 `update user set state='offline';`。
