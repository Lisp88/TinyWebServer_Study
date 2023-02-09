//
// Created by shotacon on 23-1-30.
//
#include <netinet/in.h>
#include "webserver.h"

int * Utils::u_pipe_fd = nullptr;
int Utils::u_epoll_fd = -1;

void WebServer::init(int port , string user, string password, string database, int sql_num, int thread_num) {
    m_port = port;
    m_password = password;
    m_user = user;
    m_database = database;
    m_sql_num= sql_num;
    m_thread_num = thread_num;
}

void WebServer::Thread_pool() {
    cout<<"线程池实例化"<<endl;
    m_thraed_pool = new Thread_Pool<Http_Connect>(m_sql_pool, m_thread_num);
}

void WebServer::Sql_pool() {
    cout<<"数据库连接池实例化"<<endl;
    m_sql_pool = Sql_Connection_Pool::Get_instance();
    m_sql_pool->init("localhost",3306, m_user, m_password, m_database, m_sql_num);

    m_users->Init_mysql_result(m_sql_pool);
}

void WebServer::Event_listen() {
    cout<<"创建服务器套接字，绑定，监听"<<endl;
    //创建套接字
    m_listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    //TODO 优雅关闭链接
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
    cout<<"创建epoll监听树，将服务器套接字挂载"<<endl;
    //epoll
    epoll_event events[MAX_EVENT_NUM];
    m_epoll = epoll_create(5);
    utils.Add_fd(m_epoll, m_listen_fd, 0, false);
    Http_Connect::m_epoll_fd = m_epoll;
    utils.Setnoblock(m_pipe[1]);
    utils.Add_fd(m_epoll, m_pipe[0], 0, false);
    cout<<"创建通信管道挂载到监听树上，设置信号捕捉"<<endl;
    utils.Add_sig(SIGPIPE, SIG_IGN);
    utils.Add_sig(SIGALRM, utils.Sig_handler, false);
    //utils.Add_sig()

    utils.u_pipe_fd = m_pipe;
    utils.u_epoll_fd = m_epoll;
}

void WebServer::Event_loop() {
    bool time_out = false;
    bool server_stop = false;
    cout<<"epoll_wait开始轮询监听节点"<<endl;
    while(!server_stop){
        int number = epoll_wait(m_epoll, events, MAX_EVENT_NUM, 3);
        if(number < 0 && errno != EINTR){
            //LOG
            break;
        }
        for (int i = 0; i < number; ++i) {
            int sock_fd = events[i].data.fd;
            if(sock_fd == m_listen_fd){
                cout<<"服务器套接字读事件就绪，";
                bool flag = Deal_client_data();
                if(!flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                cout<<"客户端RDHUP事件就绪，对端关闭，删除定时器"<<endl;
                Utils_Timer * timer = m_users_timer[sock_fd].timer;
                del_timer(timer, sock_fd);
            }
            else if(sock_fd == m_pipe[0] && events[i].events & EPOLLIN){
                cout<<"管道读事件就绪，准备轮寻定时器链表"<<endl;
                bool flag = deal_signal(time_out, server_stop);
            }
            else if(events[i].events & EPOLLIN){
                cout<<"客户端读事件就绪，";
                deal_read(sock_fd);
            }
            else if(events[i].events & EPOLLOUT){
                cout<<"客户端写事件就绪，";
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
        cout<<"LT 处理新客户端连接，并设置定时器"<<endl;
        int conn_fd = accept(m_listen_fd, (sockaddr*)&client_address, &sock_len);
        if(conn_fd < 0){
            //LOG
            cout<<"webserver >> deal_client_data >> accept conn < 0"<<endl;
            return false;
        }
        if(Http_Connect::m_user_count >= MAX_FD){
            cout<<"webserver >> deal_client_data >> user count over"<<endl;
            close(conn_fd);
            return false;
        }
        timer(conn_fd, client_address);
    }else{
        while(1){
            cout<<"ET 处理新客户端连接，并设置定时器"<<endl;
            int conn_fd = accept(m_listen_fd, (sockaddr*)&client_address, &sock_len);
            if(conn_fd < 0){
                //LOG
                cout<<"webserver >> deal_client_data >> accept conn < 0"<<endl;
                return false;
            }
            if(Http_Connect::m_user_count >= MAX_FD){
                cout<<"webserver >> deal_client_data >> user count over"<<endl;
                close(conn_fd);
                return false;
            }
            timer(conn_fd, client_address);
        }
    }
    return false;
}

void WebServer::timer(int conn_fd, struct sockaddr_in client_address){
    m_users[conn_fd].Init(conn_fd, client_address, m_root, m_conn_trig, m_user, m_password, m_database);

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
        cout<<"同步读，客户端对应到Http连接封装传入任务队列"<<endl;
        m_thraed_pool->append(m_users + sock_fd, 0);
        //若子线程读取/写入失败，主线程删除定时器
        while(1){
            if(1 == m_users[sock_fd].m_imprv){
                if(1 == m_users[sock_fd].m_timer_trig){
                    cout<<"检测子线程读取/写入失败，主线程删除定时器"<<endl;
                    del_timer(timer, sock_fd);
                    m_users[sock_fd].m_timer_trig = 0;
                    cout<<"read fail del timer"<<endl;
                }
                m_users[sock_fd].m_imprv = 0;
                break;
            }
        }
    }else{
        //proactor
    }
}

void WebServer::deal_write(int sock_fd) {
    Utils_Timer * timer = m_users_timer[sock_fd].timer;
    //reactor
    if(m_actor_mod == 0){
        if(timer)
            utils.m_timer_list.Adjust_timer(timer);//延长定时器
        cout<<"同步写，客户端对应到Http连接封装传入任务队列"<<endl;

        m_thraed_pool->append(m_users + sock_fd, 1);

        while(1){
            if(1 == m_users[sock_fd].m_imprv){
                if(1 == m_users[sock_fd].m_timer_trig){
                    del_timer(timer, sock_fd);
                    m_users[sock_fd].m_timer_trig = 0;
                    cout<<"write done del timer"<<endl;
                }
                m_users[sock_fd].m_imprv = 0;
                break;
            }
        }
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



