#include "config.h"

int main(int argc, char ** argv) {

    Config config;
    config.parse_arg(argc, argv);

    WebServer webserver;
    webserver.init(config.PORT, "root", "88888888", "yourdb",
                   config.actor_model, config.OPT_LINGER, config.close_log, config.close_log,config.sql_num, config.thread_num);
    //日志生成
    webserver.Log_write();
    //数据库连接池
    webserver.Sql_pool();
    //线程池
    webserver.Thread_pool();
    //触发模式设置
    webserver.Trig_mode();
    //监听设置
    webserver.Event_listen();
    //循环监听
    webserver.Event_loop();

    return 0;
}
