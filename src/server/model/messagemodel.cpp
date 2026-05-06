#include "messagemodel.hpp"
#include "db.h"
#include <cstdlib>
#include <cstdio>

bool MessageModel::insert(long long messageid, int senderid, int receiverid, int groupid, int msgtype, const string &payload)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    string escapedPayload = mysql.escapeString(payload);
    char sql[8192] = {0};
    snprintf(sql, sizeof(sql),
             "insert into chat_message(message_id, sender_id, receiver_id, group_id, msg_type, payload, status) "
             "values(%lld, %d, %d, %d, %d, '%s', %d)",
             messageid, senderid, receiverid, groupid, msgtype, escapedPayload.c_str(), MessageStatus::CREATED);
    return mysql.update(sql);
}

bool MessageModel::markDelivered(long long messageid, int receiverid)
{
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "update chat_message set status = if(status < %d, %d, status), "
             "delivered_at = if(delivered_at is null, now(), delivered_at) "
             "where message_id = %lld and receiver_id = %d and status < %d",
             MessageStatus::DELIVERED, MessageStatus::DELIVERED, messageid, receiverid, MessageStatus::ACKED);

    MySQL mysql;
    if (mysql.connect())
    {
        return mysql.update(sql);
    }
    return false;
}

bool MessageModel::ack(long long messageid, int receiverid)
{
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "update chat_message set status = %d, acked_at = now() "
             "where message_id = %lld and receiver_id = %d",
             MessageStatus::ACKED, messageid, receiverid);

    MySQL mysql;
    if (mysql.connect())
    {
        return mysql.update(sql);
    }
    return false;
}

vector<StoredMessage> MessageModel::queryUnacked(int receiverid, int limit)
{
    char sql[1024] = {0};
    snprintf(sql, sizeof(sql),
             "select message_id, payload, status from chat_message "
             "where receiver_id = %d and status < %d order by message_id asc limit %d",
             receiverid, MessageStatus::ACKED, limit);

    vector<StoredMessage> messages;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                StoredMessage message;
                message.messageid = atoll(row[0]);
                message.payload = row[1] == nullptr ? "" : row[1];
                message.status = row[2] == nullptr ? MessageStatus::CREATED : atoi(row[2]);
                messages.push_back(message);
            }
            mysql_free_result(res);
        }
    }
    return messages;
}
