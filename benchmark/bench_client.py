#!/usr/bin/env python3
"""
Async benchmark client for muduo-distributed-chat-server.
Protocol: one JSON frame per line, i.e. json.dumps(obj) + "\n".
"""
import argparse
import asyncio
import json
import random
import statistics
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional

LOGIN_MSG = 1
LOGIN_MSG_ACK = 2
REG_MSG = 4
REG_MSG_ACK = 5
ONE_CHAT_MSG = 6
GROUP_CHAT_MSG = 10
MSG_ACK = 11


def now_ms() -> int:
    return int(time.time() * 1000)


def percentile(values: List[float], p: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    idx = min(len(ordered) - 1, int(len(ordered) * p / 100.0))
    return ordered[idx]


@dataclass
class Metrics:
    connected: int = 0
    connect_failed: int = 0
    login_ok: int = 0
    login_failed: int = 0
    registered: int = 0
    register_failed: int = 0
    sent: int = 0
    received: int = 0
    acked: int = 0
    errors: int = 0
    latencies_ms: List[float] = field(default_factory=list)

    def summary(self) -> Dict[str, float]:
        avg = statistics.mean(self.latencies_ms) if self.latencies_ms else 0.0
        return {
            "connected": self.connected,
            "connect_failed": self.connect_failed,
            "login_ok": self.login_ok,
            "login_failed": self.login_failed,
            "registered": self.registered,
            "register_failed": self.register_failed,
            "sent": self.sent,
            "received": self.received,
            "acked": self.acked,
            "errors": self.errors,
            "avg_latency_ms": round(avg, 3),
            "p95_latency_ms": round(percentile(self.latencies_ms, 95), 3),
            "p99_latency_ms": round(percentile(self.latencies_ms, 99), 3),
        }


class BenchClient:
    def __init__(self, host: str, port: int, userid: Optional[int], password: str, metrics: Metrics):
        self.host = host
        self.port = port
        self.userid = userid
        self.password = password
        self.name = f"bench_{userid}" if userid is not None else "bench"
        self.metrics = metrics
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self.read_task: Optional[asyncio.Task] = None
        self.login_event = asyncio.Event()
        self.login_success = False
        self.register_event = asyncio.Event()
        self.registered_id: Optional[int] = None
        self.running = True

    async def connect(self) -> bool:
        try:
            self.reader, self.writer = await asyncio.open_connection(self.host, self.port)
            self.metrics.connected += 1
            self.read_task = asyncio.ensure_future(self.read_loop())
            return True
        except Exception:
            self.metrics.connect_failed += 1
            return False

    async def close(self):
        self.running = False
        if self.writer is not None:
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except Exception:
                pass
        if self.read_task is not None:
            self.read_task.cancel()

    async def send_json(self, obj: Dict) -> bool:
        if self.writer is None:
            return False
        try:
            self.writer.write((json.dumps(obj, ensure_ascii=False) + "\n").encode())
            await self.writer.drain()
            return True
        except Exception:
            self.metrics.errors += 1
            return False

    async def register(self) -> Optional[int]:
        await self.send_json({"msgid": REG_MSG, "name": self.name, "password": self.password})
        try:
            await asyncio.wait_for(self.register_event.wait(), timeout=5)
        except asyncio.TimeoutError:
            self.metrics.register_failed += 1
        return self.registered_id

    async def login(self) -> bool:
        await self.send_json({"msgid": LOGIN_MSG, "id": self.userid, "password": self.password})
        try:
            await asyncio.wait_for(self.login_event.wait(), timeout=5)
        except asyncio.TimeoutError:
            self.metrics.login_failed += 1
            return False
        return self.login_success

    async def chat(self, toid: int, content: str) -> bool:
        ok = await self.send_json({
            "msgid": ONE_CHAT_MSG,
            "id": self.userid,
            "name": self.name,
            "toid": toid,
            "msg": content,
            "client_ts_ms": now_ms(),
            "time": time.strftime("%Y-%m-%d %H:%M:%S"),
        })
        if ok:
            self.metrics.sent += 1
        return ok

    async def ack(self, messageid):
        if self.userid is None:
            return
        ok = await self.send_json({"msgid": MSG_ACK, "id": self.userid, "messageid": messageid})
        if ok:
            self.metrics.acked += 1

    async def read_loop(self):
        assert self.reader is not None
        while self.running:
            try:
                line = await self.reader.readline()
                if not line:
                    return
                msg = json.loads(line.decode(errors="ignore"))
                await self.handle_message(msg)
            except asyncio.CancelledError:
                return
            except Exception:
                self.metrics.errors += 1

    async def handle_message(self, msg: Dict):
        msgid = msg.get("msgid")
        if msgid == LOGIN_MSG_ACK:
            if msg.get("errno") == 0:
                self.login_success = True
                self.metrics.login_ok += 1
                # ACK login response offline messages.
                for item in msg.get("offlinemsg", []):
                    try:
                        offline_msg = json.loads(item)
                        if "messageid" in offline_msg:
                            await self.ack(offline_msg["messageid"])
                    except Exception:
                        self.metrics.errors += 1
            else:
                self.metrics.login_failed += 1
            self.login_event.set()
            return

        if msgid == REG_MSG_ACK:
            if msg.get("errno") == 0:
                self.registered_id = int(msg.get("id"))
                self.metrics.registered += 1
            else:
                self.metrics.register_failed += 1
            self.register_event.set()
            return

        if msgid in (ONE_CHAT_MSG, GROUP_CHAT_MSG):
            self.metrics.received += 1
            sent_ts = msg.get("client_ts_ms")
            if isinstance(sent_ts, (int, float)):
                self.metrics.latencies_ms.append(max(0.0, now_ms() - float(sent_ts)))
            if "messageid" in msg:
                await self.ack(msg["messageid"])
            return


async def register_users(args):
    metrics = Metrics()
    sem = asyncio.Semaphore(args.concurrency)

    async def one(i: int):
        async with sem:
            client = BenchClient(args.host, args.port, None, args.password, metrics)
            client.name = f"bench_{i}_{random.randint(1000, 9999)}"
            if await client.connect():
                await client.register()
            await client.close()

    started = time.time()
    await asyncio.gather(*(one(i) for i in range(args.users)))
    elapsed = time.time() - started
    print(json.dumps({"mode": "register", "elapsed_sec": round(elapsed, 3), **metrics.summary()}, ensure_ascii=False, indent=2))


async def chat_users(args):
    metrics = Metrics()
    clients: List[BenchClient] = []
    userids = list(range(args.start_id, args.start_id + args.users))

    sem = asyncio.Semaphore(args.concurrency)

    async def connect_and_login(userid: int):
        async with sem:
            client = BenchClient(args.host, args.port, userid, args.password, metrics)
            if await client.connect() and await client.login():
                clients.append(client)
            else:
                await client.close()

    started = time.time()
    await asyncio.gather(*(connect_and_login(userid) for userid in userids))
    if len(clients) < 2:
        print(json.dumps({"error": "less than 2 clients logged in", **metrics.summary()}, ensure_ascii=False, indent=2))
        return

    async def sender(client: BenchClient):
        for i in range(args.messages_per_user):
            toid = random.choice(userids)
            while toid == client.userid:
                toid = random.choice(userids)
            await client.chat(toid, f"bench-message-{client.userid}-{i}")
            if args.interval_ms > 0:
                await asyncio.sleep(args.interval_ms / 1000.0)

    await asyncio.gather(*(sender(client) for client in clients))
    await asyncio.sleep(args.drain_seconds)
    elapsed = time.time() - started

    for client in clients:
        await client.close()

    qps = metrics.sent / elapsed if elapsed > 0 else 0.0
    print(json.dumps({"mode": "chat", "elapsed_sec": round(elapsed, 3), "send_qps": round(qps, 3), **metrics.summary()}, ensure_ascii=False, indent=2))


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--mode", choices=["register", "chat"], required=True)
    parser.add_argument("--users", type=int, default=100)
    parser.add_argument("--start-id", type=int, default=1)
    parser.add_argument("--password", default="123456")
    parser.add_argument("--messages-per-user", type=int, default=100)
    parser.add_argument("--concurrency", type=int, default=100)
    parser.add_argument("--interval-ms", type=int, default=0)
    parser.add_argument("--drain-seconds", type=float, default=3.0)
    return parser.parse_args()


async def main():
    args = parse_args()
    if args.mode == "register":
        await register_users(args)
    else:
        await chat_users(args)


if __name__ == "__main__":
    loop = asyncio.get_event_loop()
    try:
        loop.run_until_complete(main())
    finally:
        loop.close()
