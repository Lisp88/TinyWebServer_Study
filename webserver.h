//
// Created by shotacon on 23-1-30.
//

#ifndef WEBSERVER_STUDY_WEBSERVER_H
#define WEBSERVER_STUDY_WEBSERVER_H

#include "sys/epoll.h"
#include "sys/socket.h"

#include "http_connect/http_connect.h"
#include "sql_pool/sql_connect_pool.h"
#include "thread_poll/thread_poll.h"
#include "timer/timers.h"

const int MAX_FD = 65536;
const int MAX_EVENT_NUM = 10000;
const int TIMESLOT = 5;

extern std::map<string, string> users;
class WebServer{
public:
    WebServer();
    ~WebServer();

    void init(int port , string user, string passWord, string database, int sql_num, int thread_num);
    //实例化线程池
    void Thread_pool();
    //实例化数据库连接池
    void Sql_pool();
    //事件监听(epoll循环前的准备工作)
    void Event_listen();
    //监听循环
    void Event_loop();
    //处理客户端数据
    bool Deal_client_data();
private:
    void del_timer(Utils_Timer * timer, int sock_fd);

    bool deal_signal(bool & time_out, bool & server_stop);

    //初始化客户端连接，挂载到epoll上，并设置定时器
    void timer(int conn_fd, struct sockaddr_in client_address);
    //处理监听到的读事件
    void deal_read(int sock_fd);
    //处理监听到的写事件
    void deal_write(int sock_fd);
public:
    int m_port;
    int m_pipe[2];
    int m_epoll;
    int m_actor_mod;

    Http_Connect * m_users;//一个用户对应一个http链接
    //数据库
    Sql_Connection_Pool * m_sql_pool;
    string m_user;
    string m_password;
    string m_database;
    int m_sql_num;
    char * m_root;

    //线程池相关
    Thread_Pool<Http_Connect> * m_thraed_pool;
    int m_thread_num;

    //epoll
    epoll_event events[MAX_EVENT_NUM];
    int m_listen_fd;
    int m_listen_trig;
    int m_conn_trig;

    //定时器
    Utils utils;
    client_data * m_users_timer;
};


#endif //WEBSERVER_STUDY_WEBSERVER_H
