#ifndef MESSAGEMODEL_H
#define MESSAGEMODEL_H

#include <string>
#include <vector>
using namespace std;

// 可靠消息状态：created=已入库，delivered=已投递到接收方连接，acked=客户端已确认收到
class MessageStatus
{
public:
    static const int CREATED = 0;
    static const int DELIVERED = 1;
    static const int ACKED = 2;
};

struct StoredMessage
{
    long long messageid = 0;
    string payload;
    int status = MessageStatus::CREATED;
};

// 可靠消息表的数据操作类
class MessageModel
{
public:
    // 插入一条待投递消息，payload 为最终下发给客户端的 JSON 文本
    bool insert(long long messageid, int senderid, int receiverid, int groupid, int msgtype, const string &payload);

    // 消息已被服务端投递到接收者所在连接
    bool markDelivered(long long messageid, int receiverid);

    // 客户端 ACK 确认收到消息
    bool ack(long long messageid, int receiverid);

    // 查询接收方未 ACK 的消息，用于登录/断线重连后的未读同步
    vector<StoredMessage> queryUnacked(int receiverid, int limit = 1000);
};

#endif
