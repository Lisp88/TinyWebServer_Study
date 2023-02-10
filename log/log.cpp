//
// Created by shotacon on 23-2-10.
//

#include "log.h"

bool Log::Init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    //阻塞队列大小>0，则为异步写入
    if(max_queue_size > 0){
        m_is_async = true;
        block_queue = new Block_queue<string>(max_queue_size);
        pthread_t tid;

        pthread_create(&tid, nullptr, Log_thread, nullptr);
    }
    m_close_log = close_log;
    m_log_buff_size = log_buf_size;
    m_buff = new char[m_log_buff_size];
    memset(m_buff, '\0', m_log_buff_size);
    m_lines_count = split_lines;

    time_t t = time(nullptr);
    struct tm * sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char * p = strchr(file_name, '/');
    char log_real_name[256]{0};
    //文件名不包含目录
    if(!p){
        snprintf(log_real_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }else{
        strcpy(m_log_name, p+1);
        strncpy(m_dir_name, file_name, p-file_name+1);
    }
    m_today = my_tm.tm_mday;

    m_fp = fopen(log_real_name, "a");
    if(!m_fp)
        return false;
    return true;
}

void Log::Flush() {
    m_lock.lock();
    fflush(m_fp);
    m_lock.unlock();
}

void Log::Write_log(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level)
    {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }
    //写入一个log，对m_count++, m_split_lines最大行数
    m_lock.lock();
    m_lines_count++;

    if (m_today != my_tm.tm_mday || m_lines_count % m_max_lines == 0) //everyday log
    {

        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", m_dir_name, tail, m_log_name);
            m_today = my_tm.tm_mday;
            m_lines_count = 0;
        }
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", m_dir_name, tail, m_log_name, m_lines_count / m_max_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_lock.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;
    m_lock.lock();

    //写入的具体时间内容格式
    int n = snprintf(m_buff, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

    int m = vsnprintf(m_buff + n, m_log_buff_size - 1, format, valst);
    m_buff[n + m] = '\n';
    m_buff[n + m + 1] = '\0';
    log_str = m_buff;

    m_lock.unlock();

    if (m_is_async && !block_queue->Full())
    {
        block_queue->Push(log_str);
    }
    else
    {
        m_lock.lock();
        fputs(log_str.c_str(), m_fp);
        m_lock.unlock();
    }

    va_end(valst);
}
