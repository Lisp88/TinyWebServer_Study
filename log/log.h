//
// Created by shotacon on 23-2-10.
//

#ifndef WEBSERVER_STUDY_LOG_H
#define WEBSERVER_STUDY_LOG_H

#include <cstdio>
#include <string>
#include "cstring"
#include "stdarg.h"
#include "sys/time.h"

#include "../lock/lock.h"
#include "block_queue.h"

using std::string;

class Log{
public:
    //使用懒汉局部变量
    static Log * Get_instance(){
        static Log log;
        return &log;
    }

    static void * Log_thread(void * arg){
        Log::Get_instance()->Write_async();
        return nullptr;
    }

    bool Init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void Write_log(int level, const char * format, ...);

    void Flush();
private:
    //单例
    Log(){
        m_lines_count = 0;
        m_is_async = false;
    }
    ~Log(){
        if(!m_fp) fclose(m_fp);
    }
    //多线程在队列中取走日志写入文件
    void * Write_async(){
        string a_log;
        while(block_queue->Pop(a_log)){
            m_lock.lock();
            fputs(a_log.c_str(), m_fp);
            m_lock.unlock();
        }
        return nullptr;
    }

    char m_dir_name[100];//路径
    char m_log_name[100];//文件名
    int m_max_lines;//最大行数
    int m_log_buff_size;//日志缓冲区大小
    long long m_lines_count;//行数记录
    int m_today;//记录天数
    FILE * m_fp;
    char * m_buff;
    bool m_is_async;//同步标志
    locker m_lock;
    Block_queue<string> * block_queue;
    int m_close_log;
};

#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::Get_instance()->Write_log(0, format, ##__VA_ARGS__); Log::Get_instance()->Flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::Get_instance()->Write_log(1, format, ##__VA_ARGS__); Log::Get_instance()->Flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::Get_instance()->Write_log(2, format, ##__VA_ARGS__); Log::Get_instance()->Flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::Get_instance()->Write_log(3, format, ##__VA_ARGS__); Log::Get_instance()->Flush();}
#endif //WEBSERVER_STUDY_LOG_H
