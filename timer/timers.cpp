//
// Created by shotacon on 23-2-2.
//

#include "timers.h"

void Utils::Init(int time_slot) {
    m_time_slot = time_slot;
}

int Utils::Setnoblock(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return 0;
}

void Utils::Add_fd(int epoll_fd, int fd, int triger_mod, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;

    if(triger_mod == 1){
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }else
        event.events = EPOLLIN | EPOLLRDHUP;

    if(one_shot)
        event.events |= EPOLLONESHOT;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    Setnoblock(fd);
}

void Utils::Sig_handler(int sig) {
    int msg = sig;
    //全双工管道，向写端写入数据
    send(u_pipe_fd[1], (char*)&msg, sizeof(msg), 0);

}

void Utils::Add_sig(int sig, void (handler)(int), bool restart) {
    struct sigaction sa;
    sa.sa_handler = handler;

    if(restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    if(sigaction(sig, &sa, nullptr) != 0)
        throw logic_error("timers >> add_sig >> sigaction fail");
}

void Utils::Timer_handler() {
    m_timer_list.tick();

    alarm(m_time_slot);
}

void Sort_Timer_Lst::Add_timer(Utils_Timer *timer) {
    if(!timer) return;

    if(!m_head){
        m_head = m_tail = timer;
        return;
    }
    //当前时间最小，替代头
    if(timer->m_expire < m_head->m_expire){
        timer->m_next = m_head;
        m_head->m_pre = timer;
        m_head = timer;
        return;
    }
    //正常情况，遍历插入正确位置
    Add_timer(timer, m_head);
}

void Sort_Timer_Lst::Del_timer(Utils_Timer *timer) {
    if(!timer) return;

    //为头和尾
    if(timer == m_head && timer == m_tail){
        delete timer;
        m_head = nullptr;
        m_tail = nullptr;
        return;
    }
    //为头
    if(timer == m_head){
        m_head = m_head->m_next;
        m_head->m_pre = nullptr;
        delete timer;
        return;
    }
    //为尾
    if(timer == m_tail){
        m_tail = m_tail->m_pre;
        m_tail->m_next = nullptr;
        delete timer;
        return;
    }
    //正常情况，接前后
    timer->m_pre->m_next = timer->m_next;
    timer->m_next->m_pre = timer->m_pre;
    delete timer;
}

void Sort_Timer_Lst::Adjust_timer(Utils_Timer *timer) {
    if(!timer) return;
    //调整位置，删除在添加
    Utils_Timer * tmp = timer->m_next;
    if(!tmp || tmp->m_expire > timer->m_expire) return;
    //头
    if(timer == m_head){
        m_head = m_head->m_next;
        m_head->m_pre = nullptr;
        timer->m_next = nullptr;
        Add_timer(timer, m_head);
    }
    //非头
    else{
        timer->m_pre->m_next = timer->m_next;
        timer->m_next->m_pre = timer->m_pre;
        Add_timer(timer, m_head);
    }

}
//寻找合适位置添加
void Sort_Timer_Lst::Add_timer(Utils_Timer *timer, Utils_Timer *head) {
    Utils_Timer * pre = m_head;
    Utils_Timer * tmp = pre->m_next;

    while(tmp){
        if(tmp->m_expire > timer->m_expire){
            pre->m_next = timer;
            timer->m_pre = pre;

            timer->m_next = tmp;
            tmp->m_pre = timer;
            break;
        }
        pre = tmp;
        tmp = tmp->m_next;
    }
    if(!tmp){
        //插入尾部
        m_tail->m_next = timer;
        timer->m_pre = m_tail;
        timer->m_next = nullptr;

        m_tail = timer;
    }
}

void Sort_Timer_Lst::tick() {
    if(!m_head) return;

    time_t cur = time(nullptr);
    Utils_Timer * tmp = m_head;
    while(tmp){
        if(tmp->m_expire > cur) break;

        tmp->cb_fun(tmp->m_user_data);
        m_head = tmp->m_next;
        if(!m_head)
            m_head->m_pre = nullptr;

        delete tmp;
        tmp = m_head;
    }
}

Sort_Timer_Lst::~Sort_Timer_Lst() {
    Utils_Timer * tmp = m_head;

    while(tmp){
        m_head = tmp->m_next;
        delete tmp;
        tmp = m_head;
    }
    m_head = nullptr;
    m_tail = nullptr;
}

#include "../http_connect/http_connect.h"
void cb_fun(client_data * user_data){
    epoll_ctl(Utils::u_epoll_fd, EPOLL_CTL_DEL, user_data->sock_fd, 0);

    close(user_data->sock_fd);
    Http_Connect::m_user_count--;
}
