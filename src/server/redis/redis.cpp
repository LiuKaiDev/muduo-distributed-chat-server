#include "redis.hpp"
#include <iostream>
using namespace std;

Redis::Redis()
    : _publish_context(nullptr), _subcribe_context(nullptr)
    // 但在构造函数执行时，连接还没建立，所以不能乱指向一块未知内存，最安全的做法就是先设为 nullptr。
{
}

Redis::~Redis()
{
    if (_publish_context != nullptr)
    {
        redisFree(_publish_context);
    }

    if (_subcribe_context != nullptr)
    {
        redisFree(_subcribe_context);
    }
}

bool Redis::connect()
{
    // 负责publish发布消息的上下文连接
    _publish_context = redisConnect("127.0.0.1", 6379);
    if (nullptr == _publish_context)
    {
        cerr << "connect redis failed!" << endl;
        return false;
    }

    // 负责subscribe订阅消息的上下文连接
    _subcribe_context = redisConnect("127.0.0.1", 6379);
    if (nullptr == _subcribe_context)
    {
        cerr << "connect redis failed!" << endl;
        return false;
    }

    // 在单独的线程中，监听通道上的事件，有消息给业务层进行上报
    // 新开一个线程，在这个线程里执行 observer_channel_message()，专门去监听 Redis 订阅通道上的消息
    thread t([&]() {
        observer_channel_message();
    });
    // 把这个线程和当前线程分离
    /*
    这个新线程独立运行
    当前线程不会等它执行完
    它会自己在后台一直跑
    publish：发一下就结束
    subscribe：要一直等消息到来
    */
    t.detach();

    cout << "connect redis-server success!" << endl;

    return true;
}

// 向redis指定的通道channel发布消息
bool Redis::publish(int channel, string message)
{
    // 
    /*
    redisReply 是 hiredis 里表示 Redis 命令返回结果 的回复对象类型。
    redisCommand 是 hiredis 的同步 API，用来 发送一条 Redis 命令并拿到回复；成功时返回回复对象，失败时返回 NULL。
    freeReplyObject(reply) 就是用来 释放这次命令返回的 reply 对象内存。官方示例里每次拿到 reply 后也都会调用它。
    这段代码的作用就是：
    用 _publish_context 这条 Redis 连接，执行一条 PUBLISH channel message 命令，把消息发到指定通道；如果命令执行成功，
    就释放回复对象并返回 true，否则返回 false。redisCommand 本身就是这种“发命令并返回 reply”的接口。
    */
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "PUBLISH %d %s", channel, message.c_str());
    if (nullptr == reply)
    {
        cerr << "publish command failed!" << endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

// 向redis指定的通道subscribe订阅消息
// 往 Redis 发一条 SUBSCRIBE channel 命令，让当前这条订阅连接开始订阅某个通道；真正收消息不在这里做，而是在后台监听线程里做。
bool Redis::subscribe(int channel)
{
    // SUBSCRIBE命令本身会造成线程阻塞等待通道里面发生消息，这里只做订阅通道，不接收通道消息
    // 通道消息的接收专门在observer_channel_message函数中的独立线程中进行
    // 只负责发送命令，不阻塞接收redis server响应消息，否则和notifyMsg线程抢占响应资源

    // redisAppendCommand它是 hiredis 提供的一个接口，用来把命令追加到输出缓冲区，并不是像 redisCommand 那样“发送命令并立刻拿回复”。
    // hiredis 官方同步 API文档把这类接口归到 pipelining 一组：你可以先 append 多条命令，再在后面发送/取回复
    if (REDIS_ERR == redisAppendCommand(this->_subcribe_context, "SUBSCRIBE %d", channel))
    {
        cerr << "subscribe command failed!" << endl;
        return false;
    }
    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    // 把连接输出缓冲区里的数据真正写到 socket 里发给 Redis 服务器。
    int done = 0;
    while (!done)
    {
        if (REDIS_ERR == redisBufferWrite(this->_subcribe_context, &done))
        {
            cerr << "subscribe command failed!" << endl;
            return false;
        }
    }
    // redisGetReply

    return true;
}

// 向redis指定的通道unsubscribe取消订阅消息
bool Redis::unsubscribe(int channel)
{
    if (REDIS_ERR == redisAppendCommand(this->_subcribe_context, "UNSUBSCRIBE %d", channel))
    {
        cerr << "unsubscribe command failed!" << endl;
        return false;
    }
    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done)
    {
        if (REDIS_ERR == redisBufferWrite(this->_subcribe_context, &done))
        {
            cerr << "unsubscribe command failed!" << endl;
            return false;
        }
    }
    return true;
}

// 在独立线程中接收订阅通道中的消息
// 后台线程一直阻塞等 Redis 的订阅消息，一旦收到，就把消息转交给业务层。
/*
它的作用是：
用订阅连接 _subcribe_context 持续等待 Redis 返回的 reply
如果收到的是一条真正的频道消息，就从 reply 里取出：
通道号
消息内容
然后调用 _notify_message_handler(...) 上报给业务层
最后释放本次 reply 对象
这就是 Redis 层和 ChatService 层衔接起来的地方。


redisGetReply 是什么
redisGetReply 是 hiredis 的 API。官方说明里写得很清楚：
在阻塞上下文里，它会先检查有没有未消费的 reply；
如果没有，就会把输出缓冲区刷到 socket，并一直读，
直到拿到一个 reply 为止；返回值是 REDIS_OK 或 REDIS_ERR。


reply 里存了什么
reply 的类型是 redisReply*。hiredis 头文件里定义了这个结构体，里面有这些关键字段：
type：reply 类型
integer：整数回复时用
str：字符串内容
elements：如果是数组回复，表示元素个数
element：如果是数组回复，指向各个子元素的数组
*/
void Redis::observer_channel_message()
{
    redisReply *reply = nullptr;
    while (REDIS_OK == redisGetReply(this->_subcribe_context, (void **)&reply))
    {
        // 订阅收到的消息是一个带三元素的数组
        if (reply != nullptr && reply->elements >= 3 && reply->element[1] != nullptr && reply->element[2] != nullptr && reply->element[1]->str != nullptr && reply->element[2]->str != nullptr)
        {
            // 给业务层上报通道上发生的消息
            // PUBLISH 2 {"msgid":6,"id":1,"toid":2,"msg":"hello"}
            // element[0] = "message"
            // element[1] = "2"
            // element[2] = "{\"msgid\":6,...}"
            _notify_message_handler(atoi(reply->element[1]->str) , reply->element[2]->str);
        }

        freeReplyObject(reply);
    }

    cerr << ">>>>>>>>>>>>> observer_channel_message quit <<<<<<<<<<<<<" << endl;
}

/*
它的作用就是：
外部传进来一个函数 fn
Redis 类把这个函数保存到自己的成员变量 _notify_message_handler 里
以后 Redis 收到订阅消息时，就调用这个保存下来的函数
*/
void Redis::init_notify_handler(function<void(int,string)> fn)
{
    // 把外部传进来的函数 fn，保存到当前 Redis 对象的成员变量 _notify_message_handler 里。
    this->_notify_message_handler = fn;
}

/*
ChatService 注册回调给 Redis
        ↓
Redis 保存这个回调函数
        ↓
别的服务器 publish(toid, msg)
        ↓
本服务器订阅线程 observer_channel_message 收到消息
        ↓
调用 _notify_message_handler(userid, msg)
        ↓
实际回调到 ChatService::handleRedisSubscribeMessage(userid, msg)
        ↓
业务层决定：直接发 or 存离线
*/

/*

redis.cpp 整体分成 5 部分
1. 构造和析构
构造时把 _publish_context 和 _subcribe_context 先置成 nullptr
析构时如果连接存在，就调用 redisFree(...) 释放

这里的 redisContext 是 hiredis 维护的连接上下文对象，hiredis 文档明确说 redisConnect 会创建一个 redisContext，里面保存连接状态；redisFree 用来释放它。

你可以简单记成：

_publish_context：发消息用的 Redis 连接
_subcribe_context：订阅/收消息用的 Redis 连接
2. connect()

connect() 做了三件事：

连一次 Redis，得到 _publish_context
再连一次 Redis，得到 _subcribe_context
开一个独立线程，执行 observer_channel_message()

为什么要两条连接？

因为 hiredis 的订阅连接会长期阻塞等待消息，不适合和普通 publish 命令共用一条连接，所以项目里拆成：

一个负责“发”
一个负责“收”

这是当前实现最重要的设计点。

3. publish(channel, message)

这个函数的作用是：

把消息发布到指定 Redis 通道

底层调用的是 hiredis 的同步 API redisCommand。官方文档里把它列为同步接口的一部分，redisCommand 负责发送一条 Redis 命令并拿到 reply，freeReplyObject 用来释放 reply。

所以你现在可以把 publish() 理解成：

用 _publish_context 执行 PUBLISH channel message
成功后释放 reply
返回 true/false

项目里这个 channel 通常就是 目标用户 id。

也就是说：

A 给 B 发消息
如果 B 不在本机但在线
就执行 publish(B的用户id, 消息内容)
4. subscribe(channel) / unsubscribe(channel)

这两个函数的作用就是：

subscribe(channel)：订阅某个用户 id 对应的通道
unsubscribe(channel)：取消订阅某个用户 id 对应的通道

它们不是“收到消息”的地方，只是“告诉 Redis 我要监听哪个通道”。

所以登录成功时，一般会：

服务器替当前在线用户订阅他的通道

登出或异常断开时，一般会：

取消订阅这个用户通道

你现在可以直接记成：

用户在线在哪台服务器上，哪台服务器就订阅这个用户 id 对应的通道。

5. observer_channel_message()

这个函数是整个 redis.cpp 的核心。

它运行在独立线程里，一直阻塞等待 Redis 订阅消息。
底层用的是 redisGetReply，hiredis 文档里说明它会从连接中读取 reply，在阻塞上下文里会一直等到拿到一个 reply 或出错为止。

你项目里拿到的 reply 对于 Pub/Sub 消息，通常是一个三元素数组，Redis 官方文档描述的格式是：

"message"
channel
payload

也就是：

element[0] = "message"
element[1] = 通道号
element[2] = 消息内容

所以这里代码做的事就是：

从 reply 里取出通道号
从 reply 里取出消息内容
调用 _notify_message_handler(userid, msg) 上报给业务层
再 freeReplyObject(reply) 释放这次 reply 的内存
init_notify_handler() 在整条链里的位置

这个函数是把 Redis 层和业务层接起来的关键。

它做的事非常简单：

外部传进来一个函数 fn
Redis 把这个函数保存到 _notify_message_handler

后面 observer_channel_message() 一收到订阅消息，就调用这个回调，把消息交给业务层。

你可以把它记成：

提前注册“Redis 收到消息后交给谁处理”。

在这个项目里，这个“谁”通常就是 ChatService::handleRedisSubscribeMessage(userid, msg)。

整体调用链，你今天就记这一条
用户登录
-> 当前服务器 subscribe(用户id)

A 给 B 发消息
-> 本机找不到 B，但查库发现 B 在线
-> publish(B的用户id, 消息内容)

B 所在服务器之前订阅了 B 的通道
-> observer_channel_message() 收到 Redis 推送
-> 从 reply 里取出 channel 和 message
-> 调用 _notify_message_handler(userid, msg)
-> 实际回调到 ChatService::handleRedisSubscribeMessage(userid, msg)
-> 业务层决定：直接 send 给 B，或者存离线消息
你今天收尾时，只要能讲出这 4 句话就够了

第一句：

connect() 建两条 Redis 连接，一条发消息，一条收消息。

第二句：

publish() 把消息发到目标用户 id 对应的 Redis 通道。

第三句：

subscribe() / unsubscribe() 用来让当前服务器替在线用户订阅或取消订阅对应通道。

第四句：

observer_channel_message() 在后台线程里持续收订阅消息，收到后通过回调交给 ChatService 处理。
*/