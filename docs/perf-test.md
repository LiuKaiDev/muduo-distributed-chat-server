## 基础测试

项目提供 `benchmark/bench_client.py`，用于模拟多客户端连接、登录和单聊消息收发，主要用于验证注册登录、消息投递和 ACK 流程。

```bash
python3 benchmark/bench_client.py \
  --host 127.0.0.1 \
  --port 6000 \
  --mode register \
  --users 100 \
  --password 123456 \
  --concurrency 50

python3 benchmark/bench_client.py \
  --host 127.0.0.1 \
  --port 6000 \
  --mode chat \
  --start-id 1 \
  --users 100 \
  --password 123456 \
  --messages-per-user 20 \
  --concurrency 50
