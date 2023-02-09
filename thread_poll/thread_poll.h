//
// Created by shotacon on 23-1-27.
//

#ifndef WEBSERVER_STUDY_THREAD_POLL_H
#define WEBSERVER_STUDY_THREAD_POLL_H

#include <list>
#include <pthread.h>
#include <exception>
#include <iostream>
#include "../lock/lock.h"
#include "../sql_pool/sql_connect_pool.h"

using std::list;
using std::logic_error;

//线程池类模板（不写.cpp防止模板实例化时找不到模板定义）
/// \tparam T 任务对象类型
template<class T>
class Thread_Pool{
public:
    Thread_Pool(Sql_Connection_Pool * sql_conn_pool, int thread_num = 8, int max_task_num = 10000);
    ~Thread_Pool();

    bool append(T * task, int state);
private:
    static void * work(void * arg);
    void run();

private:
    int m_thread_num;       //线程数
    int m_max_task_num;     //最大任务数
    pthread_t * m_threads;  //线程池数组
    list<T* > m_task_queue;
    Sql_Connection_Pool * m_sql_conn_pool;
    locker m_locker;
    sem m_sem;

    int m_actor_mod;
};

template<typename T>
Thread_Pool<T>::Thread_Pool(Sql_Connection_Pool *sql_conn_pool, int thread_num, int max_task_num)
:m_sql_conn_pool(sql_conn_pool), m_thread_num(thread_num), m_max_task_num(max_task_num){
    if(thread_num <= 0 || max_task_num <= 0){
        throw logic_error("thread_pool >> Thread_Pool >> arg error");
    }
    m_threads = new pthread_t[thread_num];
    if(!m_threads)
        throw logic_error("thread_pool >> Thread_Pool >> pthread array new fail");
    for (int i = 0; i < thread_num; ++i) {
        if(pthread_create(m_threads + i, nullptr, work, this) != 0){
            delete[] m_threads;
            throw logic_error("thread_pool >> Thread_Pool >> pthread_create fail ");
        }
        if(pthread_detach(m_threads[i]) != 0){
            delete[] m_threads;
            throw logic_error("thread_pool >> Thread_Pool >> pthread_detach fail");
        }
    }
}

template<typename T>
Thread_Pool<T>::~Thread_Pool() {
    delete[] m_threads;
}

template<typename T>
bool Thread_Pool<T>::append(T *task, int state) {
    m_locker.lock();
    if(m_task_queue.size() >= m_max_task_num){
        m_locker.unlock();
        return false;
    }
    task->m_state = state;
    m_task_queue.push_back(task);
    m_locker.unlock();
    m_sem.post();
    return true;
}

template<typename T>
void * Thread_Pool<T>::work(void *arg) {
    Thread_Pool * thread_pool = (Thread_Pool *)arg;
    thread_pool->run();
    return thread_pool;
}

template<typename T>
void Thread_Pool<T>::run() {
    while(1){
        m_sem.wait();   //线程们起初挂起在这儿，当sem post时才有一个线程被唤醒
        m_locker.lock();
        if(m_task_queue.empty()){
            m_locker.unlock();
            continue;
        }
        T * task = m_task_queue.front();
        m_task_queue.pop_front();
        m_locker.unlock();
        if(!task)
            continue;
        cout<<"子线程拿取任务队列的任务"<<endl;
        if(m_actor_mod == 0) {//同步
            if(task->m_state == 0){
                cout<<"任务为同步读，";
                if(task->Read_once()){
                    task->m_imprv = 1;//?
                    cout<<"数据库池大小"<<Sql_Connection_Pool::Get_instance()->Get_size();
                    Connection_RAII mysql_conn(&(task->m_mysql_conn), m_sql_conn_pool);
                    task->Process();
                }else{
                    //读取失败
                    task->m_imprv = 1;
                    task->m_timer_trig = 1;
                }
            }else{
                cout<<"任务为同步写，";
                if(task->Write()){
                    task->m_imprv = 1;
                }else{
                    task->m_imprv = 1;
                    task->m_timer_trig = 1;
                }
            }
        }else{
            //异步
        }
    }
}
#endif //WEBSERVER_STUDY_THREAD_POLL_H
