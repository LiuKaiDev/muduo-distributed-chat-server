# 应用层协议

## 传输帧

每条消息是一行 JSON：

```text
json.dumps(message) + "\n"
```

服务端在 muduo Buffer 中按 `\n` 循环拆帧，因此可以处理：

- 一次 read 只读到半个 JSON
- 一次 read 读到多个 JSON
- JSON 内容中存在空格、中文等字符

## 单聊消息

```json
{
  "msgid": 6,
  "id": 1,
  "name": "alice",
  "toid": 2,
  "msg": "hello",
  "time": "2026-05-06 20:00:00"
}
```

服务端收到后补充：

```json
{
  "messageid": 1840000000000000000,
  "need_ack": true
}
```

## ACK 消息

```json
{
  "msgid": 11,
  "id": 2,
  "messageid": 1840000000000000000
}
```

## 心跳消息

客户端登录后每 30 秒发送一次心跳：

```json
{
  "msgid": 12,
  "id": 2
}
```

服务端返回：

```json
{
  "msgid": 12,
  "errno": 0,
  "id": 2,
  "server_time": "20260506 21:30:00"
}
```

## 登录返回离线/未读消息

```json
{
  "msgid": 2,
  "errno": 0,
  "id": 2,
  "name": "bob",
  "offlinemsg": ["{...}"]
}
```

`offlinemsg` 中每个元素都是一个字符串化后的 JSON 消息，客户端展示后需要继续发送 ACK。

## 异常请求

登录和注册请求缺少必要字段时，服务端会返回对应 ACK，并带上 `errno != 0` 与 `errmsg`。聊天、加好友、入群、ACK 等登录后操作会校验连接是否已经绑定到该用户，未绑定连接的请求会被拒绝并记录日志。
