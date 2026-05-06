#include "offlinemessagemodel.hpp"
#include "db.h"
#include <cstdio>

// 存储用户的离线消息。兼容旧 offlinemessage 表，可靠消息以 chat_message 表为准。
void OfflineMsgModel::insert(int userid, string msg)
{
    MySQL mysql;
    if (mysql.connect())
    {
        string escapedMsg = mysql.escapeString(msg);
        char sql[8192] = {0};
        snprintf(sql, sizeof(sql), "insert into offlinemessage values(%d, '%s')", userid, escapedMsg.c_str());
        mysql.update(sql);
    }
}

// 删除用户的离线消息
void OfflineMsgModel::remove(int userid)
{
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql), "delete from offlinemessage where userid=%d", userid);

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

// 查询用户的离线消息
vector<string> OfflineMsgModel::query(int userid)
{
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql), "select message from offlinemessage where userid = %d", userid);

    vector<string> vec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while((row = mysql_fetch_row(res)) != nullptr)
            {
                vec.push_back(row[0]);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}
