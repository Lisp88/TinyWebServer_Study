//
// Created by shotacon on 23-1-27.
//处理http链接类

#ifndef WEBSERVER_STUDY_HTTP_CONNECT_H
#define WEBSERVER_STUDY_HTTP_CONNECT_H

#include "stdexcept"
#include "iostream"
#include "map"
#include "sys/epoll.h"
#include "netinet/in.h"
#include "fcntl.h"
#include "string.h"
#include "unistd.h"
#include "sys/stat.h"
#include "sys/mman.h"
#include "stdio.h"
#include "stdarg.h"
#include "stdlib.h"
#include "sys/uio.h"


#include "../sql_pool/sql_connect_pool.h"

using std::logic_error;
using std::cout;
using std::endl;
using std::pair;

extern std::map<string, string> users;

class Http_Connect {
public:
    static const int FILE_NAME_LEN = 200;
    static const int READ_BUFF_SIZE = 2048;
    static const int WRITE_BUFF_SIZE = 1024;

    //分别是请求方法，主状态机 和 从状态机
    enum METHOD {
        GET = 0,
        POST
    };
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum LINE_STATE {
        LINE_OK = 0,
        LINE_BAD,//该行不完整
        LINE_OPEN//该行未读取完成
    };
    enum HTTP_CODE{
        NO_REQUEST = 0,//请求不完整
        GET_REQUEST,
        POST_REQUEST,
        BAD_REQUEST,//报文语法错误
        //网页资源
        NO_RESOURCE,
        FORBIDDEN_REQUEST,//没读取权限
        FILE_REQUEST,//资源正常
        INTERNAL_ERROR//服务器内部错误

    };
public:
    void
    Init(int sock_fd, const sockaddr_in &address, char *root, int trig_mod, string user, string passwd, string sqlname);
    //对读/写缓冲区到数据进行操作
    void Process();
    //读取客户端发来的数据
    bool Read_once();
    //发送响应报文
    bool Write();
    //关闭链接
    void Close_connect(bool real_close = true);
private:
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    //解析请求报文
    LINE_STATE parse_line();
    char * get_line(){return m_read_buf + m_start_line;}
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    //生成回复报文
    HTTP_CODE do_request();//分析请求的文件
    //生成回复报文，向写缓冲区中写入数据
    bool add_response(const char *format, ...);
    bool add_status_line(int status, const char *title);

    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

    bool add_content(const char *content);


    void init();
    void unmap();
public:
    //初始化数据库读取表
    void Init_mysql_result(Sql_Connection_Pool *sql_pool);

    int m_state;//读 0, 写 1

public:
    static int m_epoll_fd;
    static int m_user_count;
    //定时器
    int m_imprv;
    int m_timer_trig;

    MYSQL * m_mysql_conn;
private:
    //线程
    locker m_lock;

    //epoll
    int m_sock;
    struct sockaddr_in m_address;
    char *m_doc_root;
    int m_trig_mod;

    //数据库
    char m_sql_passwd[100];
    char m_sql_user[100];
    char m_sql_name[100];

    //同步读写缓冲区
    char m_read_buf[READ_BUFF_SIZE];
    int m_read_idx;//指向数据末尾下一个字节
    char m_write_buf[WRITE_BUFF_SIZE];
    int m_write_idx;
    struct iovec m_iv[2];//两个向量，用于指向数据缓冲区
    int m_iv_count;
    int bytes_to_send;
    int bytes_have_send;

    //状态机
    CHECK_STATE m_check_state;
    int m_check_idx;
    int m_start_line;//行开始位置
    METHOD m_method;

    //http解析信息
    char * m_url;
    char * m_version;
    char * m_host;
    char * m_string;//存储消息体
    int cgi;//post
    int m_content_len;
    bool m_link;//长链接true
    char m_real_file[FILE_NAME_LEN];//存储资源目录
    struct stat m_file_stat;
    char * m_file_address;
};

#endif //WEBSERVER_STUDY_HTTP_CONNECT_H
