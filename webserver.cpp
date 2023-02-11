//
// Created by shotacon on 23-1-30.
//
#include <netinet/in.h>
#include "webserver.h"

int * Utils::u_pipe_fd = nullptr;
int Utils::u_epoll_fd = -1;

void WebServer::init(int port , string user, string password, string database, int actor_mod, int linger, int close_log, int trig_mod, int sql_num, int thread_num) {
    m_port = port;
    m_password = password;
    m_user = user;
    m_database = database;
    m_sql_num= sql_num;
    m_thread_num = thread_num;
    m_linger = linger;
    m_actor_mod = actor_mod;
    m_close_log = close_log;
    m_mod_trig = trig_mod;
}

void WebServer::Thread_pool() {
    LOG_INFO("线程池初始化，初始线程数量 %d", m_thread_num);
    m_thraed_pool = new Thread_Pool<Http_Connect>(m_sql_pool, m_thread_num);
}

void WebServer::Sql_pool() {
    LOG_INFO("数据库连接池实例化, 数据库链接数量 %d", m_sql_num);
    m_sql_pool = Sql_Connection_Pool::Get_instance();
    m_sql_pool->init("localhost",3306, m_user, m_password, m_database, m_sql_num);

    m_users->Init_mysql_result(m_sql_pool);
}

void WebServer::Event_listen() {
    LOG_INFO("创建服务器套接字，绑定，监听");
    //创建套接字
    m_listen_fd = socket(PF_INET, SOCK_STREAM, 0);

    if (0 == m_linger)//优雅
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listen_fd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_linger)//限时优雅
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listen_fd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag;
    setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    if(bind(m_listen_fd, (sockaddr *)&address, sizeof(address)))
        throw logic_error("webserver >> Event_listen >> bind fail");
    if(listen(m_listen_fd, 5))
        throw logic_error("webserver >> Event_listen >> listen fail");

    //设置定时超时
    utils.Init(TIMESLOT);
    LOG_INFO("创建epoll监听树，将服务器套接字挂载");
    //epoll
    epoll_event events[MAX_EVENT_NUM];
    m_epoll = epoll_create(5);
    utils.Add_fd(m_epoll, m_listen_fd, 0, false);
    Http_Connect::m_epoll_fd = m_epoll;
    utils.Setnoblock(m_pipe[1]);
    utils.Add_fd(m_epoll, m_pipe[0], 0, false);
    LOG_INFO("创建通信管道挂载到监听树上，设置信号捕捉");
    utils.Add_sig(SIGPIPE, SIG_IGN);
    utils.Add_sig(SIGALRM, utils.Sig_handler, false);
    //utils.Add_sig()

    utils.u_pipe_fd = m_pipe;
    utils.u_epoll_fd = m_epoll;
}

void WebServer::Event_loop() {
    bool time_out = false;
    bool server_stop = false;
    LOG_INFO("epoll_wait开始轮询监听节点");
    while(!server_stop){
        int number = epoll_wait(m_epoll, events, MAX_EVENT_NUM, 3);
        if(number < 0 && errno != EINTR){
            LOG_ERROR("epoll wait 失败!");
            break;
        }
        for (int i = 0; i < number; ++i) {
            int sock_fd = events[i].data.fd;
            if(sock_fd == m_listen_fd){
                LOG_INFO("服务器套接字读事件就绪，");
                bool flag = Deal_client_data();
                if(!flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                LOG_INFO("客户端RDHUP事件就绪，对端关闭，删除定时器");
                Utils_Timer * timer = m_users_timer[sock_fd].timer;
                del_timer(timer, sock_fd);
            }
            else if(sock_fd == m_pipe[0] && events[i].events & EPOLLIN){
                LOG_INFO("管道读事件就绪，准备轮寻定时器链表");
                bool flag = deal_signal(time_out, server_stop);
            }
            else if(events[i].events & EPOLLIN){
                LOG_INFO("客户端读事件就绪，");
                deal_read(sock_fd);
            }
            else if(events[i].events & EPOLLOUT){
                LOG_INFO("客户端写事件就绪，");
                deal_write(sock_fd);
            }
        }
        if(time_out){
            utils.Timer_handler();
            time_out = false;
        }
    }
}

bool WebServer::Deal_client_data() {
    struct sockaddr_in client_address;
    socklen_t sock_len = sizeof(client_address);
    if(m_listen_trig == 0)//LT
    {
        LOG_INFO("LT 处理新客户端连接，并设置定时器");
        int conn_fd = accept(m_listen_fd, (sockaddr*)&client_address, &sock_len);
        if(conn_fd < 0){
            LOG_ERROR("%s:errno is:%d", "accept 错误", errno);
            return false;
        }
        if(Http_Connect::m_user_count >= MAX_FD){
            LOG_ERROR("服务器繁忙，客户端链接达到上限");
            close(conn_fd);
            return false;
        }
        timer(conn_fd, client_address);
    }else{
        while(1){
            LOG_INFO("ET 处理新客户端连接，并设置定时器");
            int conn_fd = accept(m_listen_fd, (sockaddr*)&client_address, &sock_len);
            if(conn_fd < 0){
                LOG_ERROR("%s:errno is:%d", "accept 错误", errno);
                return false;
            }
            if(Http_Connect::m_user_count >= MAX_FD){
                LOG_ERROR("服务器繁忙，客户端链接达到上限");
                close(conn_fd);
                return false;
            }
            timer(conn_fd, client_address);
        }
    }
    return false;
}

void WebServer::timer(int conn_fd, struct sockaddr_in client_address){
    m_users[conn_fd].Init(conn_fd, client_address, m_root, m_conn_trig, m_close_log, m_user, m_password, m_database);

    m_users_timer[conn_fd].address = client_address;
    m_users_timer[conn_fd].sock_fd = conn_fd;

    Utils_Timer * timer = new Utils_Timer;
    timer->m_user_data = &m_users_timer[conn_fd];
    timer->cb_fun = cb_fun;

    time_t cur = time(nullptr);
    timer->m_expire = cur + 3 * TIMESLOT;
    m_users_timer[conn_fd].timer = timer;
    utils.m_timer_list.Add_timer(timer);
}

void WebServer::del_timer(Utils_Timer *timer, int sock_fd) {
    timer->cb_fun(&m_users_timer[sock_fd]);
    if(timer)
        utils.m_timer_list.Del_timer(timer);
    LOG_INFO("删除client : %d，定时器", sock_fd);
}

bool WebServer::deal_signal(bool &time_out, bool &server_stop) {
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipe[0], signals, sizeof(signals), 0);
    if(ret > 0) {
        for(int i = 0; i < ret; ++i){
            switch (signals[i]) {
                case SIGALRM :{
                    time_out = true;
                    break;
                }
                case SIGTERM :{
                    server_stop = true;
                    break;
                }
            }
        }
        return true;
    }
        return false;
}

void WebServer::deal_read(int sock_fd) {
    Utils_Timer * timer = m_users_timer[sock_fd].timer;
    //reactor
    if(m_actor_mod == 0){
        if(timer)
            utils.m_timer_list.Adjust_timer(timer);//延长定时器
        LOG_INFO("同步读，客户端对应到Http连接封装传入任务队列 client(%d)", sock_fd);
        m_thraed_pool->append(m_users + sock_fd, 0);
        //若子线程读取/写入失败，主线程删除定时器
        while(1){
            if(1 == m_users[sock_fd].m_imprv){
                if(1 == m_users[sock_fd].m_timer_trig){
                    LOG_INFO("检测子线程同步读取失败，主线程删除 client(%d)定时器", sock_fd);
                    del_timer(timer, sock_fd);
                    m_users[sock_fd].m_timer_trig = 0;
                }
                m_users[sock_fd].m_imprv = 0;
                break;
            }
        }
    }else{
        //proactor
        if(m_users[sock_fd].Read_once()){
            LOG_INFO("异步读，主线程读取完成之后将http对象放入任务队列 client(%s)", inet_ntoa(m_users[sock_fd].Get_address()->sin_addr));
            //异步，读IO完成之后在进行操作
            m_thraed_pool->append_p(m_users + sock_fd);
            if(timer){
                adjust_timer(timer);
            }
        }else
            del_timer(timer, sock_fd);
    }
}

void WebServer::adjust_timer(Utils_Timer *timer)
{
    time_t cur = time(NULL);
    timer->m_expire = cur + 3 * TIMESLOT;
    utils.m_timer_list.Adjust_timer(timer);

    LOG_INFO("重新调整client : %d 定时器", timer->m_user_data->sock_fd);
}

void WebServer::deal_write(int sock_fd) {
    Utils_Timer * timer = m_users_timer[sock_fd].timer;
    //reactor
    if(m_actor_mod == 0){
        if(timer)
            utils.m_timer_list.Adjust_timer(timer);//延长定时器
        LOG_INFO("同步写，客户端对应的Http对象传入任务队列, client(%d)", sock_fd);

        m_thraed_pool->append(m_users + sock_fd, 1);

        while(1){
            if(1 == m_users[sock_fd].m_imprv){
                if(1 == m_users[sock_fd].m_timer_trig){                    LOG_INFO("检测子线程读取/写入失败，主线程删除 client(%d)定时器", sock_fd);
                    LOG_INFO("检测子线程同步写入失败，主线程删除 client(%d)定时器", sock_fd);
                    del_timer(timer, sock_fd);
                    m_users[sock_fd].m_timer_trig = 0;

                }
                m_users[sock_fd].m_imprv = 0;
                break;
            }
        }
    }else{
        if(m_users[sock_fd].Write()){
            LOG_INFO("异步写,主线程发送后，重新注册读事件(%s)", inet_ntoa(m_users[sock_fd].Get_address()->sin_addr));
            //异步，写IO完成之后在进行操作
            if(timer){
                adjust_timer(timer);
            }
        }else
            del_timer(timer, sock_fd);
    }

    return;
}

WebServer::WebServer() {
    //http_conn类对象
    m_users = new Http_Connect[MAX_FD];

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    m_users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epoll);
    close(m_listen_fd);
    close(m_pipe[0]);
    close(m_pipe[1]);

    delete [] m_users;
    delete [] m_users_timer;
    delete m_thraed_pool;
}

void WebServer::Trig_mode() {
    //LT + LT
    if (0 == m_mod_trig)
    {
        LOG_INFO("%s", "触发模式设置:m_listen : LT, m_conn : LT");
        m_listen_fd = 0;
        m_conn_trig = 0;
    }
        //LT + ET
    else if (1 == m_mod_trig)
    {
        LOG_INFO("%s", "触发模式设置:m_listen : LT, m_conn : ET");
        m_listen_fd = 0;
        m_conn_trig = 1;
    }
        //ET + LT
    else if (2 == m_mod_trig)
    {
        LOG_INFO("%s", "触发模式设置:m_listen : ET, m_conn : LT");
        m_listen_fd = 1;
        m_conn_trig = 0;
    }
        //ET + ET
    else if (3 == m_mod_trig)
    {
        LOG_INFO("%s", "触发模式设置:m_listen : ET, m_conn : ET");
        m_listen_fd = 1;
        m_conn_trig = 1;
    }
}

void WebServer::Log_write() {
    if(0 == m_close_log){
        if(1 == m_log_write)
            Log::Get_instance()->Init("../ServerLog", m_close_log, 8192, 5000000, 800);
        else
            Log::Get_instance()->Init("../ServerLog", m_close_log, 8192, 5000000, 0);
    }
}



