#ifndef PUBLIC_H
#define PUBLIC_H

/*
server和client的公共消息类型定义。
说明：网络传输使用一行一个JSON帧，即 json.dump() + '\n'，用于解决TCP粘包/拆包问题。
*/
enum EnMsgType
{
    LOGIN_MSG = 1,       // 登录消息
    LOGIN_MSG_ACK,       // 登录响应消息
    LOGINOUT_MSG,        // 注销消息
    REG_MSG,             // 注册消息
    REG_MSG_ACK,         // 注册响应消息
    ONE_CHAT_MSG,        // 一对一聊天消息
    ADD_FRIEND_MSG,      // 添加好友消息

    CREATE_GROUP_MSG,    // 创建群组
    ADD_GROUP_MSG,       // 加入群组
    GROUP_CHAT_MSG,      // 群聊天

    MSG_ACK = 11,        // 客户端确认收到可靠消息
    HEARTBEAT_MSG = 12   // 心跳保活
};

#endif
