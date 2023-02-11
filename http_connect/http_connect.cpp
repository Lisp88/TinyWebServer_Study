//
// Created by shotacon on 23-1-27.
//

#include "http_connect.h"

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

std::map<string, string> users;
int Http_Connect::m_epoll_fd = -1;
int Http_Connect::m_user_count = 0;

int Setnoblock(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return 0;
}

void Add_fd(int epoll_fd, int fd, int trig_mod, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;

    if(trig_mod == 1){
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }else
        event.events = EPOLLIN | EPOLLRDHUP;

    if(one_shot)
        event.events |= EPOLLONESHOT;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    Setnoblock(fd);
}

void Mod_fd(int epoll_fd, int sock_fd, int trig_mod, int ev){
    epoll_event event;
    event.data.fd = sock_fd;

    if(trig_mod == 1)
        event.events = ev | EPOLLONESHOT | EPOLLET | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sock_fd, &event);
}

void Remove_fd(int epoll_fd, int sock_fd){
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_fd, nullptr);
    close(sock_fd);
}

void Http_Connect::Init_mysql_result(Sql_Connection_Pool *sql_pool) {
    MYSQL * conn = nullptr;
    Connection_RAII sql_conn(&conn, sql_pool);

    //检索数据库表，获取用户名和密码
    if(mysql_query(conn, "SELECT username, passwd FROM user")){
        throw logic_error("http_connect >> Init_mysql_result >> mysql_query fail");
    }

    //存储到结果集中
    MYSQL_RES * res = mysql_store_result(conn);
    //获取列数
    //int column = mysql_num_fields(res);
    //从结果中获取行进行赋值
    while (MYSQL_ROW row = mysql_fetch_row(res))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

void Http_Connect::Init(int sock_fd, const sockaddr_in &address, char * root, int trig_mod, int close_log, string user, string passwd, string sqlname) {
    m_sock = sock_fd;
    m_address = address;
    trig_mod = 1;
    Add_fd(m_epoll_fd, sock_fd, trig_mod, true);
    m_user_count++;

    m_doc_root = root;
    m_trig_mod = trig_mod;
    m_close_log = close_log;

    strcpy(m_sql_user, user.c_str());
    strcpy(m_sql_name, sqlname.c_str());
    strcpy(m_sql_passwd, passwd.c_str());

    init();
}


bool Http_Connect::Read_once() {
    if(m_read_idx >= READ_BUFF_SIZE) return false;

    int read_bytes = 0;
    //LT读取数据，一次读取即可
    if(m_trig_mod == 0){
        read_bytes = recv(m_sock, m_read_buf + m_read_idx, READ_BUFF_SIZE - m_read_idx, 0);
        m_read_idx += read_bytes;

        if(read_bytes <= 0) return false;

        return true;
    }else{
        LOG_INFO("ET，从内核读缓冲区读取到用户读缓冲区上");
        //ET读取数据，因为只触发一次，所以要读取完全部数据
        while(1){
            read_bytes = recv(m_sock, m_read_buf + m_read_idx, READ_BUFF_SIZE - m_read_idx, 0);

            if(read_bytes == -1){
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            if(read_bytes == 0) return false;

            m_read_idx += read_bytes;
        }
        return true;
    }
}

void Http_Connect::Process() {
    LOG_INFO("对读入的信息进行解析");
    //读取解析请求报文
    HTTP_CODE read_ret = process_read();
    //数据不完整，还需继续接收
    if (read_ret == NO_REQUEST) {
        Mod_fd(m_epoll_fd, m_sock, m_trig_mod, EPOLLIN);
        return;
    }
    LOG_INFO("报文格式正确，根据获取的信息生成了响应报文");
    //生成发送响应报文
    bool write_ret = process_write(read_ret);
    if (!write_ret)
        Close_connect();
    LOG_INFO("注册该客户端套接字的写事件，等待发送");
    Mod_fd(m_epoll_fd, m_sock, m_trig_mod, EPOLLOUT);
}

Http_Connect::HTTP_CODE Http_Connect::process_read() {
    LINE_STATE line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    LOG_INFO("进入Http请求报文解析的状态机，获取报文中的信息");

    //第一次解析 或 第一行数据完整才可进入
    while(m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK
    || (line_status = parse_line()) == LINE_OK){
        text = get_line();
        m_start_line = m_check_idx;
        LOG_INFO("line content : %s", text);
        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if (ret == GET_REQUEST)
                {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST)
                    return do_request();
                line_status = LINE_OPEN;//解析post请求后更改状态来跳出循环
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}
//响应报文有 请求文件 和 请求出错。状态行和响应头 两种回复报文都有，对应写缓冲区。
//而文件报文到消息体对应mmap申请的内存
bool Http_Connect::process_write(Http_Connect::HTTP_CODE ret) {
    LOG_INFO("根据读取结果来生成相应的响应报文，写入用户写缓冲区，等待发送");
    switch (ret) {
        //内部错误 500
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
                return false;
            break;
        }
        //请求不完整
        case BAD_REQUEST:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
                return false;
            break;
        }
        //禁止访问
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
                return false;
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else
            {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void Http_Connect::Close_connect(bool real_close) {
    if(real_close && (m_sock != -1)){
        Remove_fd(m_epoll_fd, m_sock);
        LOG_INFO("client : %d close", m_sock);
        m_user_count--;
        m_sock = -1;
    }
}

Http_Connect::LINE_STATE Http_Connect::parse_line() {
    char temp;
    for (; m_check_idx < m_read_idx; ++m_check_idx) {
        temp = m_read_buf[m_check_idx];
        if (temp == '\r')
        {
            if ((m_check_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_check_idx + 1] == '\n')
            {
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_check_idx > 1 && m_read_buf[m_check_idx - 1] == '\r')
            {
                m_read_buf[m_check_idx - 1] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

Http_Connect::HTTP_CODE Http_Connect::parse_request_line(char *text) {
    m_url = strpbrk(text, " \t");//找到str2中任意字符第一次出现的位置
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    //取请求方法
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");//返回str1中第一个不是(str2中任意字符)的字符下标
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    //对于资源前带有http/s的进行特殊处理
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');//返回字符第一次出现的位置
    }
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示欢迎界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;

    LOG_INFO("解析请求行 : %s", text);
    return NO_REQUEST;
}

//逐行处理请求头直到空行停止
Http_Connect::HTTP_CODE Http_Connect::parse_headers(char *text) {
    //空行，则需判断请求方式来决定是否解析消息体
    if(text[0] == '\0'){
        LOG_INFO("解析空行，");
        if(m_content_len != 0){
            m_check_state = CHECK_STATE_CONTENT;
            LOG_INFO("POST请求，准备解析请求体");
            return NO_REQUEST;
        }
        LOG_INFO("GET请求，解析结束");
        LOG_INFO("解析请求头");
        return GET_REQUEST;
    }
    //非空行则是请求头
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_link = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_len = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    return NO_REQUEST;
}


Http_Connect::HTTP_CODE Http_Connect::parse_content(char *text) {
    if (m_read_idx >= (m_content_len + m_check_idx))
    {
        text[m_content_len] = '\0';
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}


Http_Connect::HTTP_CODE Http_Connect::do_request() {
    LOG_INFO("根据请求action值，来确定返回文件的目录");
    strcpy(m_real_file, m_doc_root);
    int len = strlen(m_doc_root);
    //printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/');

    //处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *) malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILE_NAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';
        if (*(p + 1) == '3') {
            LOG_INFO("action 3 注册验证");
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *) malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            if (users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(m_mysql_conn, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();
                if (!res){
                    strcpy(m_url, "/log.html");
                    LOG_INFO("注册通过，返回登录页面");
                }else{
                    strcpy(m_url, "/registerError.html");
                    LOG_INFO("注册失败，返回注册失败页面");
                }
            } else{
                strcpy(m_url, "/registerError.html");
                LOG_INFO("注册失败，返回注册失败页面");
            }
        }
            //如果是登录，直接判断
            //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2') {
            LOG_INFO("action 2 登录验证");
            if (users.find(name) != users.end() && users[name] == password){
                strcpy(m_url, "/welcome.html");
                LOG_INFO("登录成功，返回欢迎页面");
            }
            else {
                strcpy(m_url, "/logError.html");
                LOG_INFO("登录失败，返回登录失败页面");
            }
        }
    }
    //为m_reak_file赋值，存储资源目录
    //跳转登录页面
    if (*(p + 1) == '0')
    {
        LOG_INFO("action 0 跳转注册页面");
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }else if (*(p + 1) == '1')
    {
        LOG_INFO("action 1 跳转登录页面");
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')
    {
        LOG_INFO("action 5 跳转图片页面");
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        LOG_INFO("action 6 跳转视频页面");
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }else if (*(p + 1) == '7')
    {
        LOG_INFO("action 7 跳转关注页面");
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }else
        strncpy(m_real_file + len, m_url, FILE_NAME_LEN - len - 1);
    //将文件信息存储进结构体中
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    if(!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    //文件类型目录，返回报文有误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    LOG_INFO("使用mmap将html文件绑定到内存中，增加访问速度，文件大小: %d", m_file_stat.st_size);
    close(fd);
    return FILE_REQUEST;
}

bool Http_Connect::add_response(const char *format, ...) {
    if(m_write_idx >= WRITE_BUFF_SIZE) return false;
    va_list arg_list;//可变参数列表
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFF_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFF_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool Http_Connect::add_headers(int content_length) {
    return add_content_length(content_length) && add_linger() &&
           add_blank_line();
}

bool Http_Connect::add_status_line(int status, const char *title) {
    add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
    return false;
}

bool Http_Connect::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool Http_Connect::add_content_length(int content_length) {
    return add_response("Content-Length:%d\r\n", content_length);
}

bool Http_Connect::add_linger() {
    return add_response("Connection:%s\r\n", (m_link == true) ? "keep-alive" : "close");
}

bool Http_Connect::add_content(const char *content) {
    return add_response("%s", content);
}

void Http_Connect::init() {
    m_mysql_conn = nullptr;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_link = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_len = 0;
    m_host = 0;
    m_start_line = 0;
    m_check_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    m_timer_trig = 0;
    m_imprv = 0;

    memset(m_read_buf, '\0', READ_BUFF_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFF_SIZE);
    memset(m_real_file, '\0', FILE_NAME_LEN);
}

bool Http_Connect::Write() {

    if(bytes_to_send == 0){
        Mod_fd(m_epoll_fd, m_sock, m_trig_mod, EPOLLIN);
        init();
        return true;
    }
    int temp = 0;
    int newadd = 0;
    cout<<"写缓冲区 响应部分:"<<m_write_buf<<endl;
    cout<<"写缓冲区 文件部分:"<<m_file_address<<endl;
    while(1){
        //按顺序发送向量指向的缓冲区
        temp = writev(m_sock, m_iv, m_iv_count);
        if(temp < 0){
            //写缓冲区满了
            if(errno == EAGAIN){
                Mod_fd(m_epoll_fd, m_sock, m_trig_mod, EPOLLOUT);
                return true;
            }
            //不是缓冲区导致的发送失败
            unmap();//取消映射
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        //请求头发送完
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
        //发送完毕
        if (bytes_to_send <= 0)
        {
            LOG_INFO("发送完毕，重新注册读事件");
            unmap();
            Mod_fd(m_epoll_fd, m_sock, m_trig_mod, EPOLLIN);

            if (m_link)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
    return false;
}

void Http_Connect::unmap() {
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

