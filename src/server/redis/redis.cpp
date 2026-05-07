#include "redis.hpp"
#include <muduo/base/Logging.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>
using namespace std;

namespace
{
string getEnvOrDefault(const char *key, const string &defaultValue)
{
    const char *value = getenv(key);
    if (value == nullptr || value[0] == '\0')
    {
        return defaultValue;
    }
    return value;
}

int getEnvIntOrDefault(const char *key, int defaultValue)
{
    const char *value = getenv(key);
    if (value == nullptr || value[0] == '\0')
    {
        return defaultValue;
    }

    int parsed = atoi(value);
    return parsed > 0 ? parsed : defaultValue;
}

redisContext *connectContext(const string &host, int port)
{
    timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    redisContext *context = redisConnectWithTimeout(host.c_str(), port, timeout);
    if (context == nullptr)
    {
        LOG_ERROR << "connect redis failed: allocate context failed";
        return nullptr;
    }
    if (context->err)
    {
        LOG_ERROR << "connect redis failed: " << context->errstr;
        redisFree(context);
        return nullptr;
    }
    return context;
}

bool runStatusCommand(redisContext *context, const char *command, const string &arg)
{
    if (context == nullptr)
    {
        return false;
    }

    redisReply *reply = nullptr;
    if (arg.empty())
    {
        reply = static_cast<redisReply *>(redisCommand(context, command));
    }
    else
    {
        reply = static_cast<redisReply *>(redisCommand(context, command, arg.c_str()));
    }

    if (reply == nullptr)
    {
        LOG_ERROR << "redis command failed: " << context->errstr;
        return false;
    }

    bool ok = reply->type != REDIS_REPLY_ERROR;
    if (!ok && reply->str != nullptr)
    {
        LOG_ERROR << "redis command error: " << reply->str;
    }
    freeReplyObject(reply);
    return ok;
}

bool configureContext(redisContext *context)
{
    string password = getEnvOrDefault("CHAT_REDIS_PASSWORD", "");
    if (!password.empty() && !runStatusCommand(context, "AUTH %s", password))
    {
        return false;
    }

    int db = getEnvIntOrDefault("CHAT_REDIS_DB", 0);
    if (db > 0)
    {
        char dbName[32] = {0};
        snprintf(dbName, sizeof(dbName), "%d", db);
        if (!runStatusCommand(context, "SELECT %s", dbName))
        {
            return false;
        }
    }
    return true;
}

bool isMessageReply(redisReply *reply)
{
    return reply != nullptr &&
           reply->type == REDIS_REPLY_ARRAY &&
           reply->elements >= 3 &&
           reply->element[0] != nullptr &&
           reply->element[1] != nullptr &&
           reply->element[2] != nullptr &&
           reply->element[0]->str != nullptr &&
           strcmp(reply->element[0]->str, "message") == 0 &&
           reply->element[1]->str != nullptr &&
           reply->element[2]->str != nullptr;
}
}

Redis::Redis()
    : _publish_context(nullptr),
      _subscribe_context(nullptr),
      _connected(false)
{
}

Redis::~Redis()
{
    if (_publish_context != nullptr)
    {
        redisFree(_publish_context);
        _publish_context = nullptr;
    }

    if (_subscribe_context != nullptr)
    {
        redisFree(_subscribe_context);
        _subscribe_context = nullptr;
    }
}

bool Redis::connect()
{
    string host = getEnvOrDefault("CHAT_REDIS_HOST", "127.0.0.1");
    int port = getEnvIntOrDefault("CHAT_REDIS_PORT", 6379);

    _publish_context = connectContext(host, port);
    if (_publish_context == nullptr || !configureContext(_publish_context))
    {
        if (_publish_context != nullptr)
        {
            redisFree(_publish_context);
            _publish_context = nullptr;
        }
        return false;
    }

    _subscribe_context = connectContext(host, port);
    if (_subscribe_context == nullptr || !configureContext(_subscribe_context))
    {
        if (_subscribe_context != nullptr)
        {
            redisFree(_subscribe_context);
            _subscribe_context = nullptr;
        }
        redisFree(_publish_context);
        _publish_context = nullptr;
        return false;
    }

    _connected = true;

    thread t([this]() {
        observer_channel_message();
    });
    t.detach();

    LOG_INFO << "connect redis-server success, host=" << host << " port=" << port;
    return true;
}

bool Redis::isConnected() const
{
    return _connected &&
           _publish_context != nullptr &&
           _subscribe_context != nullptr &&
           !_publish_context->err &&
           !_subscribe_context->err;
}

bool Redis::publish(int channel, string message)
{
    if (_publish_context == nullptr || _publish_context->err)
    {
        LOG_ERROR << "publish redis message failed: redis is not connected";
        return false;
    }

    redisReply *reply = static_cast<redisReply *>(
        redisCommand(_publish_context, "PUBLISH %d %s", channel, message.c_str()));
    if (reply == nullptr)
    {
        LOG_ERROR << "publish redis message failed: " << _publish_context->errstr;
        return false;
    }

    bool ok = reply->type != REDIS_REPLY_ERROR;
    if (!ok && reply->str != nullptr)
    {
        LOG_ERROR << "publish redis message failed: " << reply->str;
    }
    freeReplyObject(reply);
    return ok;
}

bool Redis::subscribe(int channel)
{
    return sendSubscribeCommand("SUBSCRIBE", channel);
}

bool Redis::unsubscribe(int channel)
{
    return sendSubscribeCommand("UNSUBSCRIBE", channel);
}

bool Redis::sendSubscribeCommand(const char *command, int channel)
{
    if (_subscribe_context == nullptr || _subscribe_context->err)
    {
        LOG_ERROR << command << " redis channel failed: redis is not connected";
        return false;
    }

    if (REDIS_ERR == redisAppendCommand(_subscribe_context, "%s %d", command, channel))
    {
        LOG_ERROR << command << " redis channel failed: append command error";
        return false;
    }

    int done = 0;
    while (!done)
    {
        if (REDIS_ERR == redisBufferWrite(_subscribe_context, &done))
        {
            LOG_ERROR << command << " redis channel failed: " << _subscribe_context->errstr;
            return false;
        }
    }
    return true;
}

void Redis::observer_channel_message()
{
    redisReply *reply = nullptr;
    while (_subscribe_context != nullptr &&
           REDIS_OK == redisGetReply(_subscribe_context, reinterpret_cast<void **>(&reply)))
    {
        if (isMessageReply(reply))
        {
            if (_notify_message_handler)
            {
                _notify_message_handler(atoi(reply->element[1]->str), reply->element[2]->str);
            }
            else
            {
                LOG_WARN << "redis message received before notify handler initialized";
            }
        }

        if (reply != nullptr)
        {
            freeReplyObject(reply);
            reply = nullptr;
        }
    }

    _connected = false;
    LOG_ERROR << "observer_channel_message quit";
}

void Redis::init_notify_handler(function<void(int, string)> fn)
{
    _notify_message_handler = fn;
}
