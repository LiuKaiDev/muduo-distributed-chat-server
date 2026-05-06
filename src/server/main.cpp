#include "chatserver.hpp"
#include "chatservice.hpp"
#include <iostream>
#include <signal.h>
using namespace std;

/*
main.cpp 主要负责启动聊天服务器。程序首先检查命令行参数，从中解析出监听的 IP 和端口，
其中 atoi 用于把端口字符串转成整数，uint16_t 表示无符号 16 位整数，适合存端口号。
然后通过 signal(SIGINT, resetHandler) 注册中断信号处理函数，这样服务端在按下 Ctrl+C 退出时，
会先调用 ChatService::reset() 把数据库中残留的在线状态统一改为离线。
之后创建主线程的 EventLoop、监听地址 InetAddress 和 ChatServer 对象，
调用 server.start() 启动服务器监听，最后通过 loop.loop() 进入事件循环，不断处理连接和消息事件。
*/

// 处理服务器ctrl+c结束后，重置user的状态信息
void resetHandler(int)
{
    ChatService::instance()->reset();
    exit(0);
}

int main(int argc, char **argv)
{
    // argc命令行参数的个数
    // 命令行参数数组
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatServer 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    // atoi = ascii to int 把字符串转换成整数
    // uint16_t = u = unsigned，无符号 int = 整数 16 = 16 位 无符号 16 位整数 0 ~ 65535
    uint16_t port = atoi(argv[2]);
    // 给进程注册一个信号处理函数 当程序收到 Ctrl+C 产生的中断信号时，调用 resetHandler 来执行善后逻辑。
    signal(SIGINT, resetHandler);

    EventLoop loop; // 创建一个 EventLoop 对象，表示当前线程的事件循环，用来驱动网络事件处理。
    InetAddress addr(ip, port); // 根据命令行传入的 IP 和端口，构造服务器监听地址对象。
    ChatServer server(&loop, addr, "ChatServer"); // 用主事件循环 loop、监听地址 addr 和服务器名字 "ChatServer" 创建聊天服务器对象。

    server.start(); // 启动服务器，开始监听端口，准备接收客户端连接。
    loop.loop(); // 进入事件循环，不断监听和处理网络事件，程序会阻塞在这里持续运行。

    return 0;
}