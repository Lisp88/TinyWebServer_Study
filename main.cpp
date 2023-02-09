#include <iostream>
#include "webserver.h"
#include "config.h"

int main(int argc, char ** argv) {

    Config config;
    config.parse_arg(argc, nullptr);

    WebServer webserver;
    cout<<"webserver 资源初始化"<<endl;
    webserver.init(config.PORT, "root", "88888888", "yourdb",
                   config.sql_num, config.thread_num);

    webserver.Sql_pool();
    cout<<"main >> sql_poll >>map size : "<<users.size()<<endl;
    webserver.Thread_pool();
    cout<<"main >> thread_pool >>map size : "<<users.size()<<endl;
    webserver.Event_listen();
    cout<<"main >> listen >>map size : "<<users.size()<<endl;
    webserver.Event_loop();

    return 0;
}
