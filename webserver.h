#ifndef WEBSERVER_H
#define WEBSERVER_H
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

//最大文件描述符个数
const int MAX_FD = 65536;
//最大事件数
const int MAX_EVENT_NUMBER = 10000;
//最小超时单位
const int TIMESLOT = 5;

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port, string user, string password, string databaseName, int log_write,
              int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclinetdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);


public:
    //基础信息
    //服务器端口号
    int m_port;
    //根目录
    char *m_root;
    //日志类型，默认1为异步
    int m_log_write;
    //是否启动日志，默认0为开启
    int m_close_log;
    //Reactor或Proactor
    int m_actormodel;

    //网络信息
    //相互连接的套接字
    int m_pipefd[2];
    //epoll对象
    int m_epollfd;
    //单个http连接
    http_conn *users;

    //数据库相关
    connection_pool *m_connPool;
    //登陆数据库用户名
    string m_user;
    //登陆数据库密码
    string m_password;
    //使用数据库名
    string m_databaseName;
    //数据库连接池数量
    int m_sql_num;

    //线程池相关，指定参数模板为http_conn类型
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    //监听套接字
    int m_listenfd;
    //是否优雅下线
    int m_OPT_LINGER;
    //I/O复用的事件触发模式组合0、1、2、3
    int m_TRIGMode;
    //监听文件描述符的事件的ET或LT触发模式
    int m_LISTENTrigmode;
    //连接文件描述符的事件的ET或LT触发模式
    int m_CONNTrigmode;

    //客户连接资源对象
    client_data *users_timer;
    //工具类
    Utils utils;
};

#endif