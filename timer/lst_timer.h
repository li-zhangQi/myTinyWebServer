#ifndef LST_TIMER
#define LST_TIMER
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>

#include "../log/log.h"

class util_timer;

//客户连接资源
struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

//定时器类，以一个双向链表实现
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    //超时时间
    time_t expire;
    //回调函数:从内核事件表删除事件，关闭文件描述符，释放连接资源
    void (*cb_func)(client_data *);
    //连接资源
    client_data *user_data;
    //前向定时器
    util_timer *prev;
    //后继定时器
    util_timer *next;
};

//定时器容器类
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();
    //添加定时器，内部调用私有成员add_timer
    void add_timer(util_timer *timer);
    //调整定时器，任务发生变化时，调整定时器在链表中的位置
    void adjust_timer(util_timer *timer);
    //删除定时器
    void del_timer(util_timer *timer);
    //定时任务处理函数
    void tick();

private:
    //私有成员，被公有成员add_timer和adjust_time调用
    //主要用于调整链表内部结点
    void add_timer(util_timer *timer, util_timer *lst_head);
    util_timer *head;
    util_timer *tail;
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    //最小间隔时间初始化啊
    void init(int timeslot);
    //对文件描述符设置非阻塞
    int setnonblocking(int fd);
    //内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
    //信号处理函数需要在信号发生时调用，但是信号是异步事件，可能会在任何时候发生，甚至在对象还没有创建或已销毁的情况下
    //所以将信号处理函数 sig_handler 设置为静态的原因是为了兼容信号捕捉的机制
    //信号处理函数，将信号处理函数设置为静态成员函数可以在不需要对象实例的情况下调用
    static void sig_handler(int sig);
    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);
    //定时器处理函数
    void timer_handler();
    //向客户端发送错误信息
    void show_error(int connfd, const char *info);

public: 
    //管道id，设置静态变量使静态信号处理函数sig_handler能访问到该成员变量
    static int *u_pipefd;
    //升序链表定时器容器对象
    sort_timer_lst m_timer_lst;
    //epollfd
    static int u_epollfd;
    //最小时间间隙
    int m_TIMESLOT;
};

//定时器回调函数，类外定义，作为全局函数
void cb_func(client_data *user_data);

#endif