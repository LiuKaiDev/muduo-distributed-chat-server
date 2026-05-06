#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"
#include <muduo/base/Logging.h>

#include <algorithm>
#include <functional>
#include <exception>
#include <iostream>
#include <string>
using namespace std;
using namespace std::placeholders;
using json = nlohmann::json;

// 初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // muduo 多 Reactor：1个main loop负责accept，4个sub loop负责已连接socket的I/O。
    _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    _server.start();
}

// 上报链接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    if (!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    // 一行一个JSON帧：json.dump() + '\n'。
    // 这里循环拆帧，避免一次read读到半包或多个包时 json::parse 失败。
    while (buffer->readableBytes() > 0)
    {
        const char *begin = buffer->peek();
        const char *end = begin + buffer->readableBytes();
        const char *eol = std::find(begin, end, '\n');
        if (eol == end)
        {
            // 当前buffer里还没有完整一帧，等待下一次onMessage继续拼接。
            return;
        }

        string frame(begin, eol);
        buffer->retrieveUntil(eol + 1);
        if (!frame.empty() && frame.back() == '\r')
        {
            frame.pop_back();
        }
        if (frame.empty())
        {
            continue;
        }

        try
        {
            json js = json::parse(frame);
            if (!js.contains("msgid"))
            {
                LOG_ERROR << "invalid message without msgid: " << frame;
                continue;
            }
            auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
            msgHandler(conn, js, time);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR << "parse client message failed: " << e.what() << ", frame=" << frame;
        }
    }
}
