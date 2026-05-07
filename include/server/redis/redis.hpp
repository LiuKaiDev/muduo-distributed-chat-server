#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>
#include <thread>
#include <functional>
#include <string>
using namespace std;

// Redis Pub/Sub 封装：每个在线用户使用 userid 作为 channel，支持跨节点消息转发。
class Redis
{
public:
    Redis();
    ~Redis();

    // 连接redis服务器
    bool connect();
    bool isConnected() const;

    // 向redis指定的通道channel发布消息
    bool publish(int channel, string message);

    // 向redis指定的通道subscribe订阅消息
    bool subscribe(int channel);

    // 向redis指定的通道unsubscribe取消订阅消息
    bool unsubscribe(int channel);

    // 在独立线程中接收订阅通道中的消息
    /*
    为什么要单独线程？
    因为 Redis 的订阅接收通常是一个阻塞式等待过程。
    也就是说：
    它会一直等
    一直监听有没有新消息进来
    如果放在主线程里，主线程就卡住了。
    所以这里单独开线程专门干这件事。
    */
    void observer_channel_message();

    // 初始化向业务层上报通道消息的回调对象
    void init_notify_handler(function<void(int, string)> fn);

private:
    bool sendSubscribeCommand(const char *command, int channel);

    // hiredis同步上下文对象，负责publish消息
    redisContext *_publish_context;

    // hiredis同步上下文对象，负责subscribe消息
    redisContext *_subscribe_context;

    bool _connected;

    // 回调操作，收到订阅的消息，给service层上报
    function<void(int, string)> _notify_message_handler;
};

#endif
