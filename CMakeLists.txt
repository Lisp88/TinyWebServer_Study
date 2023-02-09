cmake_minimum_required(VERSION 3.22)
project(WebServer_Study)

set(CMAKE_CXX_STANDARD 14)

add_executable(WebServer_Study main.cpp thread_poll/thread_poll.h lock/lock.h sql_pool/sql_connect_pool.cpp sql_pool/sql_connect_pool.h http_connect/http_connect.cpp http_connect/http_connect.h webserver.cpp webserver.h timer/timers.cpp timer/timers.h config.h config.cpp)

target_link_libraries(WebServer_Study -lpthread -lmysqlclient)
