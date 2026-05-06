#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <atomic>
#include <chrono>
#include <unistd.h>
#include <cstdlib>
#include <vector>
using namespace std;
using namespace std::placeholders;
using namespace muduo;

namespace
{
long long generateMessageId()
{
    static atomic<unsigned int> seq{0};
    auto now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    long long pidPart = static_cast<long long>(getpid() & 0x3ff);
    long long seqPart = static_cast<long long>(seq.fetch_add(1) & 0x3ff);
    return (now << 20) | (pidPart << 10) | seqPart;
}

void sendJson(const TcpConnectionPtr &conn, const json &js)
{
    string payload = js.dump();
    payload.push_back('\n');
    conn->send(payload);
}

void sendFrame(const TcpConnectionPtr &conn, const string &payload)
{
    if (!payload.empty() && payload.back() == '\n')
    {
        conn->send(payload);
    }
    else
    {
        conn->send(payload + "\n");
    }
}

bool getLongLong(const json &js, const string &key, long long &value)
{
    if (!js.contains(key))
    {
        return false;
    }
    if (js[key].is_number_integer() || js[key].is_number_unsigned())
    {
        value = js[key].get<long long>();
        return true;
    }
    if (js[key].is_string())
    {
        value = atoll(js[key].get<string>().c_str());
        return true;
    }
    return false;
}
}

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({MSG_ACK, std::bind(&ChatService::messageAck, this, _1, _2, _3)});

    if (_redis.connect())
    {
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

// 服务器异常，业务重置方法
void ChatService::reset()
{
    _userModel.resetState();
}

// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp) {
            LOG_ERROR << "msgid:" << msgid << " can not find handler!";
        };
    }
    return it->second;
}

// 处理登录业务  id  password
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    string pwd = js["password"];

    User user = _userModel.query(id);
    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "this account is using, input another!";
            sendJson(conn, response);
            return;
        }

        {
            lock_guard<mutex> lock(_connMutex);
            _userConnMap.insert({id, conn});
        }

        _redis.subscribe(id);

        user.setState("online");
        _userModel.updateState(user);

        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        response["name"] = user.getName();

        // 新可靠消息表：查询未 ACK 消息，用于断线重连后的未读同步。
        vector<string> offlineMessages;
        vector<StoredMessage> storedMessages = _messageModel.queryUnacked(id);
        for (StoredMessage &message : storedMessages)
        {
            if (!message.payload.empty())
            {
                offlineMessages.push_back(message.payload);
                _messageModel.markDelivered(message.messageid, id);
            }
        }

        // 兼容旧 offlinemessage 表的数据，避免旧库迁移时丢消息。
        vector<string> legacyMessages = _offlineMsgModel.query(id);
        for (string &message : legacyMessages)
        {
            offlineMessages.push_back(message);
        }
        if (!legacyMessages.empty())
        {
            _offlineMsgModel.remove(id);
        }
        if (!offlineMessages.empty())
        {
            response["offlinemsg"] = offlineMessages;
        }

        vector<User> userVec = _friendModel.query(id);
        if (!userVec.empty())
        {
            vector<string> vec2;
            for (User &user : userVec)
            {
                json js;
                js["id"] = user.getId();
                js["name"] = user.getName();
                js["state"] = user.getState();
                vec2.push_back(js.dump());
            }
            response["friends"] = vec2;
        }

        vector<Group> groupuserVec = _groupModel.queryGroups(id);
        if (!groupuserVec.empty())
        {
            vector<string> groupV;
            for (Group &group : groupuserVec)
            {
                json grpjson;
                grpjson["id"] = group.getId();
                grpjson["groupname"] = group.getName();
                grpjson["groupdesc"] = group.getDesc();
                vector<string> userV;
                for (GroupUser &user : group.getUsers())
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    js["role"] = user.getRole();
                    userV.push_back(js.dump());
                }
                grpjson["users"] = userV;
                groupV.push_back(grpjson.dump());
            }

            response["groups"] = groupV;
        }

        sendJson(conn, response);
    }
    else
    {
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "id or password is invalid!";
        sendJson(conn, response);
    }
}

// 处理注册业务  name  password
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    json response;
    response["msgid"] = REG_MSG_ACK;
    if (state)
    {
        response["errno"] = 0;
        response["id"] = user.getId();
    }
    else
    {
        response["errno"] = 1;
    }
    sendJson(conn, response);
}

// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    _redis.unsubscribe(userid);

    User user(userid, "", "", "offline");
    _userModel.updateState(user);
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    if (user.getId() != -1)
    {
        _redis.unsubscribe(user.getId());
        user.setState("offline");
        _userModel.updateState(user);
    }
}

// 一对一聊天业务：入库 -> 本机投递 / Redis跨节点投递 / 等待接收者重连同步
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int fromid = js["id"].get<int>();
    int toid = js["toid"].get<int>();
    long long messageid = generateMessageId();
    js["messageid"] = messageid;
    js["need_ack"] = true;

    _messageModel.insert(messageid, fromid, toid, 0, ONE_CHAT_MSG, js.dump());

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            sendJson(it->second, js);
            _messageModel.markDelivered(messageid, toid);
            return;
        }
    }

    User user = _userModel.query(toid);
    if (user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
        return;
    }

    // 接收方离线时不再写旧 offlinemessage 表，消息已在 chat_message 表中等待重连同步。
    LOG_INFO << "store offline reliable message, messageid=" << messageid << " toid=" << toid;
}

// 添加好友业务 msgid id friendid
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    _friendModel.insert(userid, friendid);
}

// 创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    Group group(-1, name, desc);
    if (_groupModel.createGroup(group))
    {
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
}

// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupModel.addGroup(userid, groupid, "normal");
}

// 群组聊天业务：为每个接收者单独生成消息ID，便于按人 ACK 和重连同步
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

    for (int id : useridVec)
    {
        json msg = js;
        long long messageid = generateMessageId();
        msg["messageid"] = messageid;
        msg["toid"] = id;
        msg["need_ack"] = true;
        _messageModel.insert(messageid, userid, id, groupid, GROUP_CHAT_MSG, msg.dump());

        bool deliveredByLocalConnection = false;
        {
            lock_guard<mutex> lock(_connMutex);
            auto it = _userConnMap.find(id);
            if (it != _userConnMap.end())
            {
                sendJson(it->second, msg);
                _messageModel.markDelivered(messageid, id);
                deliveredByLocalConnection = true;
            }
        }
        if (deliveredByLocalConnection)
        {
            continue;
        }

        User user = _userModel.query(id);
        if (user.getState() == "online")
        {
            _redis.publish(id, msg.dump());
        }
    }
}

void ChatService::messageAck(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    long long messageid = 0;
    if (!getLongLong(js, "messageid", messageid))
    {
        LOG_ERROR << "invalid ACK without messageid";
        return;
    }
    _messageModel.ack(messageid, userid);
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    long long messageid = 0;
    try
    {
        json js = json::parse(msg);
        getLongLong(js, "messageid", messageid);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "parse redis message failed: " << e.what();
    }

    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        sendFrame(it->second, msg);
        if (messageid > 0)
        {
            _messageModel.markDelivered(messageid, userid);
        }
        return;
    }

    // 本机找不到连接，说明状态可能短暂不一致；可靠消息仍在 chat_message 表中，等待用户重连后同步。
    LOG_INFO << "redis message receiver not in local map, userid=" << userid << " messageid=" << messageid;
}
