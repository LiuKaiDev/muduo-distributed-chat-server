#include "usermodel.hpp"
#include "db.h"
#include <cstdio>
#include <iostream>
using namespace std;

// User表的增加方法
bool UserModel::insert(User &user)
{
    MySQL mysql;
    if (mysql.connect())
    {
        string name = mysql.escapeString(user.getName());
        string password = mysql.escapeString(user.getPwd());
        string state = mysql.escapeString(user.getState());

        char sql[2048] = {0};
        snprintf(sql, sizeof(sql), "insert into user(name, password, state) values('%s', '%s', '%s')",
                 name.c_str(), password.c_str(), state.c_str());
        if (mysql.update(sql))
        {
            // 获取插入成功的用户数据生成的主键id
            user.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }

    return false;
}

// 根据用户号码查询用户信息
User UserModel::query(int id)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql), "select * from user where id = %d", id);

    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                mysql_free_result(res);
                return user;
            }
        }
    }

    return User();
}

// 更新用户的状态信息
bool UserModel::updateState(User user)
{
    MySQL mysql;
    if (mysql.connect())
    {
        string state = mysql.escapeString(user.getState());
        char sql[1024] = {0};
        snprintf(sql, sizeof(sql), "update user set state = '%s' where id = %d", state.c_str(), user.getId());
        if (mysql.update(sql))
        {
            return true;
        }
    }
    return false;
}

// 重置用户的状态信息
void UserModel::resetState()
{
    // 1.组装sql语句
    char sql[1024] = "update user set state = 'offline' where state = 'online'";

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}
