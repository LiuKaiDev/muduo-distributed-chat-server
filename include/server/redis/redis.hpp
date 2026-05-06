#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h> // 这是 Redis 的 C 客户端库头文件。
#include <thread>
#include <functional>
using namespace std;

/*
redis作为集群服务器通信的基于发布-订阅消息队列时，会遇到两个难搞的bug问题，参考我的博客详细描述：
https://blog.csdn.net/QIANGWEIYUAN/article/details/97895611


每个用户登录到某台服务器后，这台服务器会订阅该用户 id 对应的 Redis 通道。
后续如果其他服务器要给这个用户转发消息，就会根据消息里的 toid，把消息 publish 到 toid 对应的通道；
订阅了这个通道的服务器收到消息后，再把消息转发给本机这个用户的连接。
*/
class Redis
{
public:
    Redis();
    ~Redis();

    // 连接redis服务器 
    bool connect();

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
    // hiredis同步上下文对象，负责publish消息
    redisContext *_publish_context;

    // hiredis同步上下文对象，负责subscribe消息
    redisContext *_subcribe_context;

    // 回调操作，收到订阅的消息，给service层上报
    function<void(int, string)> _notify_message_handler;
};

#endif
