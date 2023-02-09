//
// Created by shotacon on 23-1-27.
//
//数据库连接池

#ifndef WEBSERVER_STUDY_SQL_CONNECT_POOL_H
#define WEBSERVER_STUDY_SQL_CONNECT_POOL_H

#include <string>
#include <list>
#include "../lock/lock.h"
#include "mysql/mysql.h"

using std::string;
using std::list;

class Sql_Connection_Pool{
public:
    MYSQL * Get_sql_connect();
    bool Realse_sql_connect(MYSQL * sql_conn);
    void Destroy_pool();

    static Sql_Connection_Pool * Get_instance();

    void init(string url, unsigned port, string user, string password, string database_name, int max_conn);

    int Get_size(){return m_conn_list.size();}
private:
    Sql_Connection_Pool();
    Sql_Connection_Pool(const Sql_Connection_Pool & conn_pool);
    ~Sql_Connection_Pool();

    int m_max_conn_num;
    int m_cur_conn_num;
    int m_free_conn_num;
    locker m_lock;
    sem m_sem;
    list<MYSQL *> m_conn_list;
public:
    string m_url;
    short m_port;
    string m_user;
    string m_password;
    string m_database_name;
};

class Connection_RAII{
public:
    Connection_RAII(MYSQL ** conn, Sql_Connection_Pool * conn_pool);
    ~Connection_RAII();
private:
    MYSQL * m_conn;
    Sql_Connection_Pool * m_sql_conn_pool;
};
#endif //WEBSERVER_STUDY_SQL_CONNECT_POOL_H
