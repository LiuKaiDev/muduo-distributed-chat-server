#include "db.h"
#include <muduo/base/Logging.h>
#include <cstdlib>

static string getEnvOrDefault(const char *key, const string &defaultValue)
{
    const char *value = getenv(key);
    if (value == nullptr || value[0] == '\0')
    {
        return defaultValue;
    }
    return value;
}

static unsigned int getEnvPortOrDefault(const char *key, unsigned int defaultValue)
{
    const char *value = getenv(key);
    if (value == nullptr || value[0] == '\0')
    {
        return defaultValue;
    }
    return static_cast<unsigned int>(atoi(value));
}

// 初始化数据库连接
MySQL::MySQL()
{
    _conn = mysql_init(nullptr);
}

// 释放数据库连接资源
MySQL::~MySQL()
{
    if (_conn != nullptr)
        mysql_close(_conn);
}

// 连接数据库
bool MySQL::connect()
{
    string server = getEnvOrDefault("CHAT_DB_HOST", "127.0.0.1");
    string user = getEnvOrDefault("CHAT_DB_USER", "root");
    string password = getEnvOrDefault("CHAT_DB_PASSWORD", "123456");
    string dbname = getEnvOrDefault("CHAT_DB_NAME", "chat");
    unsigned int port = getEnvPortOrDefault("CHAT_DB_PORT", 3306);

    MYSQL *p = mysql_real_connect(_conn, server.c_str(), user.c_str(),
                                  password.c_str(), dbname.c_str(), port, nullptr, 0);
    if (p != nullptr)
    {
        mysql_query(_conn, "set names utf8mb4");
        LOG_INFO << "connect mysql success!";
    }
    else
    {
        LOG_ERROR << "connect mysql fail: " << mysql_error(_conn);
    }

    return p != nullptr;
}

// 更新操作
bool MySQL::update(string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        LOG_ERROR << __FILE__ << ":" << __LINE__ << ": "
                  << sql << " update failed: " << mysql_error(_conn);
        return false;
    }

    return true;
}

// 查询操作
MYSQL_RES *MySQL::query(string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        LOG_ERROR << __FILE__ << ":" << __LINE__ << ": "
                  << sql << " query failed: " << mysql_error(_conn);
        return nullptr;
    }

    return mysql_use_result(_conn);
}

// 获取连接
MYSQL *MySQL::getConnection()
{
    return _conn;
}

string MySQL::escapeString(const string &value)
{
    if (_conn == nullptr || value.empty())
    {
        return value;
    }

    string escaped;
    escaped.resize(value.size() * 2 + 1);
    unsigned long length = mysql_real_escape_string(_conn, &escaped[0], value.c_str(), value.size());
    escaped.resize(length);
    return escaped;
}
