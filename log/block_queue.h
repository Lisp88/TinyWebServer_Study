//
// Created by shotacon on 23-2-10.
//

#ifndef WEBSERVER_STUDY_BLOCK_QUEUE_H
#define WEBSERVER_STUDY_BLOCK_QUEUE_H

#include "queue"


#include "../lock/lock.h"

using std::queue;

template<class T>
class Block_queue{
public:
    Block_queue(int max_size):m_max_qu_size(max_size){}
    ~Block_queue(){
        while(!m_qu.empty())
            m_qu.pop();
        m_max_qu_size = 0;
    }

    bool Push(const T & item){
        m_lock.lock();
        if(m_qu.size() >= m_max_qu_size){
            m_lock.unlock();
            return false;
        }
        m_qu.push(item);

        m_c.signal();
        m_lock.unlock();
    }

    bool Pop(T & item){
        m_lock.lock();
        while(m_qu.empty()){
            m_c.wait(m_lock.get());
        }
        item = m_qu.front();
        m_qu.pop();
        m_lock.unlock();
    }

    bool Full(){
        m_lock.lock();
        if(m_qu.size() >= m_max_qu_size){
            m_lock.unlock();
            return true;
        }
        m_lock.unlock();
        return false;
    }
private:
    queue<T> m_qu;
    int m_max_qu_size;

    cond m_c;
    locker m_lock;
};


#endif //WEBSERVER_STUDY_BLOCK_QUEUE_H
