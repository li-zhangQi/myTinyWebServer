#include "webserver.h"

//服务器初始化
WebServer::WebServer()
{
    //创建http_conn类对象
    users = new http_conn[MAX_FD];

    //设置root资源文件夹的路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //含有定时器的客户连接资源
    users_timer = new client_data[MAX_FD];
}

//服务器资源释放
WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

//初始化服务器资源的端口号、数据库和各种模式的设定值
void WebServer::init(int port, string user, string password, string databaseName, int log_write,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_password = password;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

//设置epoll对象的文件描述符的事件触发模式
void WebServer::trig_mode()
{
    //监听LT + 连接LT
    if(m_TRIGMode == 0)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //监听LT + 连接ET
    else if(m_TRIGMode == 1)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //监听ET + 连接LT
    else if(m_TRIGMode == 2)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //监听ET + 连接ET
    else if(m_TRIGMode == 3)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

//初始化日志系统
void WebServer::log_write()
{
    if(m_close_log == 0)
    {
        //日志类型，默认为异步日志
        if(m_log_write == 1)
        {
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        }
        else
        {
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
        }
    }
}

//初始化数据库连接池
void WebServer::sql_pool()
{
    //单例模式获取唯一实例
    m_connPool = connection_pool::GetInstance();
    //设置数据库
    m_connPool->init("localhost", m_user, m_password, m_databaseName, 3306, m_sql_num, m_close_log);

    //读取数据库表
    users->initmysql_result(m_connPool);
}

//创建线程池
void WebServer::thread_pool()
{
    //线程池对象
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

//创建事件监听，包括网络套接字连接，Socket网络编程基础步骤实现，以及信号量的监听
void WebServer::eventListen()
{
    //创建一个用来监听的套接字
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    //延时关闭，即优雅关闭连接
    if(m_OPT_LINGER == 0)
    {
        //用于控制 SO_LINGER 选项是否启用。前1启动SO_LINGER，后为指定关闭套接字时的等待时间秒数
        struct linger tmp = {0, 1}; 
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if(m_OPT_LINGER == 1) 
    {
        //延时开启，时间1s
        struct linger tmp = {1, 1}; 
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    //置0地址执行内存
    bzero(&address, sizeof(address));
    //设定连接的端口和地址等信息
    address.sin_family = AF_INET;
    address.sin_port = htons(m_port);
    //可以接受来自任何可用网络接口的连接请求
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    int flag = 1;
    //设置地址复用
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    //为监听套接字绑定需要被监听的地址和端口号
    ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);

    //监听创建的监听套接字，等待队列最大值为5，即同一时刻的监听的套接字并发数为5
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    //设置服务器的最小时间间隙
    utils.init(TIMESLOT);

    // epoll_event events[MAX_EVENT_NUMBER];
    //epoll创建内核事件表
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    //为监听套接字注册读事件，默认不开启EPOLLONESHOT
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    //创建管道套接字
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    //管道写端用于写入信号值，同时写端设置为非阻塞
    utils.setnonblocking(m_pipefd[1]);
    //设置管道读端通过I/O复用系统监测读事件，形成统一事件源，该事件同其他文件描述符都可以通过epoll来监测
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    //添加捕捉SIGPIPE信号并忽略因管道问题停止程序
    utils.addsig(SIGPIPE, SIG_IGN);
    //添加对SIGALRM、SIGTERM信号的捕捉，交给信号集结构体变量内的信号处理函数去处理
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    //每隔TIMESLOT时间触发SIGALRM信号
    alarm(TIMESLOT);

    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

//单个定时器的创建
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    //初始化连接进来的http对象
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_password, m_databaseName);

    //初始化客户连接资源数据
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    //创建一个定时器结点，将连接信息挂载
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    //设置新对象初始过期时间
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    //定时器对象结点的传递与设置
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//对新添加的定时器在链表上的位置进行调整，若数据活跃，则将定时器节点往后延迟3个超时时间单位
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

//删除定时器结点
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    //回调函数执行删除内核事件表的监听事件和关闭http连接操作
    timer->cb_func(&users_timer[sockfd]);
    //确保被删除的指针对象存在
    if(timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

//接收处理客户端
bool WebServer::dealclinetdata()
{
    //创建用来保存客户端地址信息的传出参数
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    //判断监听套接字的事件的触发模式，0为水平触发LT
    if(m_LISTENTrigmode == 0)
    {
        //阻塞等待接收listen监听到的连接请求
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
        if(connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if(http_conn::m_user_count >= MAX_FD)
        {
            //错误信息通过该连接套接字发送给客户端
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        //将初始化好的信息挂载到定时器结点上
        timer(connfd, client_address);
    }
    //边沿触发ET
    else
    {
        //循环一次性完全接收连接
        while(1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
            if(connfd < 0 )
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if(http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

//处理信号，依据定时处理函数功能是否开启执行、服务器是否关闭作出处理
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];

    //套接字管道读端读取信号量
    //正常情况下，此ret返回值总是1，只有14或15两个ASCII码对应的字符
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    //读取信号量错误
    if(ret == -1)
    {
        return false;
    }
    //管道写端关闭
    else if(ret == 0)
    {
        return false;
    }
    else
    {
        for(int i = 0; i < ret; ++i)
        {
            //判断接收到的信号量字符的ASCII码与哪个信号量值对应
            switch (signals[i])
            {
                case SIGALRM:
                {
                    timeout = true;
                    break;
                }
                case SIGTERM:
                {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

//处理客户连接上接收到的数据，新连接加入任务队列，数据被处理则删除
void WebServer::dealwithread(int sockfd)
{
    //创建定时器对象临时结点，将客户连接资源中对应的定时器取出来
    util_timer *timer = users_timer[sockfd].timer;

    //reactor模式，整个读事件就绪的请求放在请求队列上
    if(m_actormodel == 1)
    {
        if(timer)
        {
            //新结点往后延迟3个超时时间单位
            adjust_timer(timer);
        }

        //将该连请求接放入请求队列，并标记IO事件类型为读事件
        m_pool->append(users + sockfd, 0);
        
        while(true)
        {
            //表示数据正在处理中
            if(users[sockfd].improv == 1)
            {
                //连接未关闭
                if(users[sockfd].timer_flag == 1)
                {
                    //释放资源，删除定时器结点
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    //proactor模式
    else
    {
        //先读取数据，再放进请求队列，方便线程连接
        if(users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            
            //加入到proactor模式下的请求队列
            m_pool->append_p(users + sockfd);
            if(timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

//向客户连接写入数据
void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    //reactor模式，整个写事件就绪的请求放在请求队列上
    if(m_actormodel == 1)
    {
        if(timer)
        {
            adjust_timer(timer);
        }

        //将该连请求接放入请求队列，并标记IO事件类型为写事件
        m_pool->append(users + sockfd, 1);

        while(true)
        {
            if(users[sockfd].improv == 1)
            {
                if(users[sockfd].timer_flag == 1)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    //proactor模式
    else
    {
        //写入数据，因为异步处理，且没有等待连接过程即不再需要放进请求队列
        if(users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if(timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

//事件回环，即服务器主线程
void WebServer::eventLoop()
{
    //定时处理函数功能是否开启执行
    bool timeout = false;
    //服务器是否关闭
    bool stop_server = false;

    //服务器未关闭
    while(!stop_server)
    {
        //阻塞等待监听epoll树上的被监听的文件描述符的事件发生，
        //将监听到的文件描述符和对应的事件保存在对应类型的数组events中，提供后续判断依据
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);

        //当阻塞于某个慢系统调用的一个进程捕获到某个信号且相应进入信号处理函数返回时，该系统调用可能返回一个EINTR错误
        //而直接忽略epoll_wait因alarm定时器产生的错误
        //在epoll_wait时，因为设置了alarm发送警告信号量而进入信号处理函数，导致每次返回-1，errno为EINTR，
        //对于这种错误返回进行忽略
        if(number < 0 && errno != EINTR) 
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        //对所有就绪事件进行处理，遍历保存在数组内每个被监听到的文件描述符
        for(int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //接收处理客户端，(最基础的连接都连不上，直接放弃对该文件描述符的其他事件检测)
            if(sockfd == m_listenfd)
            {
                bool flag = dealclinetdata();
                if(flag == false)
                {
                    continue;
                }
            }
            //处理异常事件，对方关闭连接，挂起，错误
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //取出客户连接资源的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理定时器信号，套接字为管道读端且检测到它的事件为读事件
            else if((sockfd == m_pipefd[0]) && events[i].events & EPOLLIN)
            {
                //处理信号，ALARM接收成功则timeout定时处理函数功能开启执行，或SIGTERM接收成功则stop_server服务器关
                bool flag = dealwithsignal(timeout, stop_server);
                if(flag == false)
                {
                    LOG_ERROR("%s", "dealwithsignal failure");
                }
            }
            //处理客户连接上接收到的数据
            else if(events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            //处理向客户连接写入数据
            else if(events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }

        //处理定时器
        if(timeout)
        {
            //为非必须事件，收到信号并不是立马处理，为完成读写事件后再进行处理,
            //因此前面的管道套接字的写端可以设置为非阻塞
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}