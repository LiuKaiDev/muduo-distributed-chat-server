create database if not exists chat default charset utf8mb4 collate utf8mb4_unicode_ci;
use chat;

create table if not exists user (
    id int primary key auto_increment,
    name varchar(50) not null,
    password varchar(50) not null,
    state enum('online','offline') default 'offline'
) engine=InnoDB default charset=utf8mb4;

create table if not exists friend (
    userid int not null,
    friendid int not null,
    primary key(userid, friendid),
    key idx_friendid(friendid)
) engine=InnoDB default charset=utf8mb4;

-- 兼容原始版本的离线消息表。新可靠消息逻辑主要使用 chat_message。
create table if not exists offlinemessage (
    userid int not null,
    message text not null,
    key idx_userid(userid)
) engine=InnoDB default charset=utf8mb4;

create table if not exists allgroup (
    id int primary key auto_increment,
    groupname varchar(50) not null,
    groupdesc varchar(200) default ''
) engine=InnoDB default charset=utf8mb4;

create table if not exists groupuser (
    groupid int not null,
    userid int not null,
    grouprole enum('creator','normal') default 'normal',
    primary key(groupid, userid),
    key idx_userid(userid)
) engine=InnoDB default charset=utf8mb4;

-- 可靠消息表：支持消息ID、ACK确认、投递状态、断线重连后的未读同步。
create table if not exists chat_message (
    message_id bigint primary key,
    sender_id int not null,
    receiver_id int not null,
    group_id int not null default 0,
    msg_type int not null,
    payload text not null,
    status tinyint not null default 0 comment '0-created, 1-delivered, 2-acked',
    created_at timestamp not null default current_timestamp,
    delivered_at timestamp null default null,
    acked_at timestamp null default null,
    key idx_receiver_status(receiver_id, status, created_at),
    key idx_sender_created(sender_id, created_at),
    key idx_group_created(group_id, created_at)
) engine=InnoDB default charset=utf8mb4;
