//
// Created by shotacon on 23-2-2.
//

#ifndef WEBSERVER_STUDY_TIMERS_H
#define WEBSERVER_STUDY_TIMERS_H

#include "iostream"
#include <netinet/in.h>
#include "fcntl.h"
#include "signal.h"
#include "stdexcept"
#include "time.h"
#include "sys/socket.h"
#include "sys/epoll.h"

using std::cout;
using std::endl;
using std::logic_error;

class Utils_Timer;

struct client_data{
    sockaddr_in address;
    int sock_fd;
    Utils_Timer * timer;
};

//定时器
class Utils_Timer{
public:
    Utils_Timer():m_pre(nullptr), m_next(nullptr){}
public:
    time_t m_expire;

    void (*cb_fun) (client_data *);
    client_data * m_user_data;
    Utils_Timer * m_pre;
    Utils_Timer * m_next;
};

//定时器容器:双向升序链表，基于超时时间
class Sort_Timer_Lst{
public:
    Sort_Timer_Lst():m_head(nullptr), m_tail(nullptr){}
    ~Sort_Timer_Lst();
    //添加定时器
    void Add_timer(Utils_Timer * timer);
    //删除定时器
    void Del_timer(Utils_Timer * timer);
    //检查超时定时器
    void tick();
    //将定时器调整相应位置（由于定时器在应用时只会延长，所以只向后遍历足以）
    void Adjust_timer(Utils_Timer * timer);
private:
    //重载添加，工具函数
    void Add_timer(Utils_Timer * timer, Utils_Timer * head);

    Utils_Timer * m_head;
    Utils_Timer * m_tail;
};

//共用工具类，内含常用工具函数和对定时器到部分应用封装
class Utils{
public:
    Utils(){}
    ~Utils(){}

    void Init(int timeslot);

    //对文件描述符设置非阻塞
    int Setnoblock(int fd);

    //将内核事件设置为读事件和ET模式，选择性设置oneshot
    void Add_fd(int epoll_fd, int fd, int triger_mod, bool one_shot);

    //信号处理函数
    static void Sig_handler(int sig);

    //设置信号
    void Add_sig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务
    void Timer_handler();

public:
    static int u_epoll_fd;
    static int * u_pipe_fd;
    int m_time_slot;
    Sort_Timer_Lst m_timer_list;
};


void cb_fun(client_data * user_data);
#endif //WEBSERVER_STUDY_TIMERS_H
