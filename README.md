# myWebServer

___

用C++实现的高性能WEB服务器，经过webbenchh压力测试可以实现上万的每秒查询率QPS

## 环境要求

___

- Linux
- C++
- MySql

## 项目启动

___

 先配置好数据库

```c++
// 建立webdb库
create database webdb;

// 创建user表
USE webdb;
CREATE TABLE user(
    username char(50) NULL,
    password char(50) NULL
)ENGINE=InnoDB;

// 添加数据
INSERT INTO user(username, password) VALUES('name', 'password');
```

```c++
make
./bin/server
```

## 支持个性化运行

___

```c++
./server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]

```

-p，自定义端口号

-l，选择日志写入方式，默认同步写入

-m，listenfd和connfd的模式组合，默认使用LT + LT

-o，优雅关闭连接，默认不使用

-s，数据库连接数量

-t，线程数量

-c，关闭日志，默认打开

-a，选择反应堆模型，默认Proactor

## 压力测试

---

```c++ 
./webbench-1.5/webbench -c 100 -t 10 http://ip:port/
./webbench-1.5/webbench -c 1000 -t 10 http://ip:port/
./webbench-1.5/webbench -c 5000 -t 10 http://ip:port/
./webbench-1.5/webbench -c 10000 -t 10 http://ip:port/
```

- 测试环境: Ubuntu:18.04 cpu:i5-8265U 内存:8G
- QPS: 电脑性能限制实测7200+ 理论有10000+



___



# 对项目的整体梳理

___

>注：
>
>T：变量类型
>
>C：参数
>
>H：函数
>
>Q：其他

___



### locker .h 头文件涉及的函数概要

互斥量：

T：pthread_mutex_t

H：pthread_mutex_init()、pthread_mutex_destroy()、pthread_mutex_lock()、pthread_mutex_unlock()

信号量：

T：sem_t

H：sem_init()、sem_destroy()、sem_wait()、sem_post()

>_wait函数将以原子操作方式将信号量减一，信号量为0时，sem_wait阻塞
>
>_post函数以原子操作方式将信号量加一，信号量大于0时，唤醒调用sem_post的线程

条件变量：

T：pthread_cond_t

H：pthread_cond_init()、pthread_cond_destroy()、pthread_cond_wait()、pthread_cond_timewait()、	

​		pthread_cond_signal()、pthread_cond_broadcast()

>条件变量需要配合锁来使用
>
>_wait等待条件变量时，线程会先释放持有的互斥锁，然后进入阻塞状态，等待其他线程通过条件变量的通知来唤		醒它。在收到通知后，线程会重新获得互斥锁，然后继续执行

### 包含的功能：

1、互斥量、信号量、条件变量的创建

___



### threadpool.h 头文件涉及函数概要

T：pthread_t

H：pthread_creat()、pthread_detach()	

>_creat函数中，通常将线程的入口点函数定义为静态函数，以满足pthread_create函数的要求。如果不使用静态	修饰该成员函数，将这个函数声明为非静态的成员函数，那么它将隐式地具有一个this指针作为其第一个参	  	数。在C++中，非静态成员函数的调用方式是通过对象调用的，编译器会在调用时将对象的地址作为this指针	传递给该函数。使用静态修饰使得worker函数成为一个独立于类对象的普通函数，不需要关心对象的this指 	  	针。正常情况下，C++ 的非静态成员函数会隐式地接受一个指向对象的this指针作为参数。因此，如果将一个	普通的成员函数指针赋值给 void *worker (void  *)，那么在调用线程函数时，this指针会替代 (void *) 这部分，	即作为隐含的参数传递给成员函数。这可能会导致调用出错或无法正常工作，我们需要的是(void this)而不是	(this)
>
>pthread_create (pthread_t *thread_tid,            //返回新生成的线程的id
>                    const pthread_attr_t *attr,             //指向线程属性的指针,通常设置为NULL
>                    void * (*start_routine) (void *),      //处理线程函数的地址
>                   void *arg);                                      //start_routine()中的参数

Q： list<T *> m_workqueue //请求队列，注意为指针类型

​		T *request = m_workqueue.front() //从请求队列中取出第一个任务

### 包含的功能：

1、创建线程，通过一个静态成员函数 static void *worker(void *arg)，实现成为工作线程

2、创建请求队列，分别在reactor模式和proactor模式下 通过信号量的通知机制将请求任务放入任务队列

3、创建工作线程的处理函数，函数先获取到线程池对象，通过此对象获取到业务处理函数

4、在业务处理函数中，若为reactor模式则调用处理读写IO事件，如果为读事件则调用循环读取用户数据，来处理请求报		文并报文响应，如果为写事件则调用函数往响应报文写入数据；若为proactor模式则直接调用处理请求报文并报文响		应，完成用户请求而无需负责数据的读写

### 设计总结：

本线程池的网络并发模型设计模式为半同步/半反应堆，其中反应堆具体为Proactor事件处理模式

具体的，主线程为异步线程，负责监听文件描述符，接收socket新连接，若当前监听的socket发生了读写事件，然后将任		务插入到请求队列。工作线程从请求队列中取出任务，完成读写数据的处理

>proactor事件处理模式中：
>
>- 主线程充当异步线程，负责监听所有socket上的事件
>- 若有新请求到来，主线程接收之以得到新的连接socket，然后往epoll内核事件表中注册该socket上的读写事件
>- 如果连接socket上有读写事件发生，主线程从socket上接收数据，并将数据封装成请求对象插入到请求队列中
>- 所有工作线程睡眠在请求队列上，当有任务到来时，通过竞争（如互斥锁）获得任务的接管权

___



### block_queue.h头文件涉及函数概要

T：struct timespec、struct timeval

H：gettimeofday()

Q ：m_back = (m_back + 1) % m_max_size;  //m_back + 1为要让出入数据的位置，取模能防止跃出队列位置

​		m_front = (m_front + 1) % m_max_size; //循环队列中，不论进队列还是出队列，尾指针或者头指针都先加1

### 包含的功能：

1、创建一个阻塞队列，利用循环数组实现

2、将异步日志所写的日志内容先存入阻塞队列（生产者）

3、写线程从阻塞队列中取出内容，写入日志。另可利用条件变量的等待时间参数实现等待（消费者）

### 设计总结：

**异步日志**，将所写的日志内容先存入阻塞队列，写线程从阻塞队列中取出内容，写入日志

**阻塞队列**，将生产者-消费者模型进行封装，使用循环数组实现队列，作为两者共享的缓冲区

**生产者-消费者模型**，生产者线程与消费者线程共享一个缓冲区，其中生产者线程往缓冲区中push消息，消费者线程从缓		冲区中pop消息

___



### log.h头文件涉及函数概要

H：fputs()

>int fputs(const char *str, FILE *stream)函数是标准C库中的一个输出函数，用于将字符串写入到指定的文件流		中。将字符串 str 写入到文件流 stream 中，直到遇到 null 终止符或写入错误。它不会在输出中添加换行		符。如果文件流操作成功，则返回写入的字符数。
>

### 包含的功能：

1、创建一个日志类，私有化其构造函数不让外界创建创建实例

2、创建一个公有的静态方法，在其内部再创建一个生成实例的静态变量

3、函数体内返回该变量的地址，提供给方法调用者获取该实例

4、创建一个获取异步写日志阻塞队列弹出需要处理的任务的方法，并将通过串接受收到的内容，传入到文件地址处

5、创建一个回调函数，此函数即为公有的静态方法，其为获取调用异步写入日志的方法，它的操作与类内属性有关，因		此内类声明和定义，并且较简单直接声明定义一起写

6、自定义输出日志的自定义宏

>void write_log(int level, const char *format, ...);	//将输出内容按照标准格式整理
>
>#define LOG_INFO(format, ...) if(条件) {Log::get_instance()->write_log(1, format, ## _VA_ARGS__); 等}

### 设计总结：

**单例模式**，保证一个类仅有一个实例，并提供一个访问它的全局访问点，该实例被所有程序模块共享

>私有化它的构造函数，以防止外界创建单例类的对象；使用类的私有静态指针变量指向类的唯一实例，并用一个		公有的静态方法获取该实例。或使用线程安全的静态局部变量实现线程安全懒汉模式

___



### log.cpp 文件涉及函数概要

T：time_t、struct tm、va_list

H：memset()、time() 、localtime()、strrchr()、strcpy()、snprintf()、fopen()、va_start()、vsnprintf()、va_end()、				fflush()、c_str()

>time函数用于获取当前的系统时间（以秒为单位）自1970年1月1日以来的秒数（UNIX时间戳）
>
>localtime函数用于将时间戳转换为本地时间，并返回一个指向 tm结构的指针
>
>snprintf函数将格式化的数据输出到字符串中
>
>vsnprintf函数用于可变参数列表的字符串格式化输出并写入缓冲区
>
>>可变参数列表相关：
>>
>>在函数定义中创建一个va_list类型的变量list
>>
>>使用宏va_start(list, format)，使的变量list初始化为一个参数列表，其实这个宏是将list指向了参数format	后面的位置，也就是可变参数列表的开头，这里的format参数用于确定从这个参数之后开始访问可变	参数，相当于一个定位器
>>
>>使用宏va_arg(list, type)访问参数列表，每访问一个参数后，list都会向后移动。这个宏检索函数参数列表	中类型为 type 的下一个参数并返回
>>
>>使用宏va_end(list)完成清理工作
>
>c_str 函数返回的是一个指向以 null 结尾的 C 风格字符串的指针，这样就可以在 C 标准库中使用这个字符串。



### 包含的功能：

1、实现日志初始化，判断如果是异步日志则创建一个阻塞队列，同时创建子线程，在其回调函数中处理阻塞队列的日志		输出

2、获取本地时间的拷贝份

3、自定义格式化日志名，根据传入的整个文件名字符串进行分析，若输入的文件名没有/，则直接将时间+文件名作为日		志名；若有/则，将文件名拆分为路径名与日志名

> 需要拆分的情况：
>
>const char *p = strrchr(file_name, '/'); //查找字符最后一次出现的位置，并返回指向该位置的指
>
>strcpy(log_name, p + 1); //指针未移动
>
>strncpy(dir_name, file_name, p - file_name + 1); //p - file_name + 1是文件所在路径文件夹的长度，因为				file_name为数组首地址，指向文件名最后一个/符的 p - file_name + 1 即为路径名

4、将格式化好的日志名作为文件路径，以追加方式打开文件，并将返回的结构体指针赋给成员变量，为后续写入该日志		做准备

5、创建一个写入日志的方法，方法的参数列表为可变参数列表，实现超行、按天分文件的业务逻辑

6、最后将完成的同步日志输入到之前返回的结构体指针指向的的文件；若是异步则将日志加入到阻塞队列等待处理

### 设计总结：

本次使用单例模式创建日志系统，对服务器运行状态、错误信息和访问数据进行记录，该系统可以实现按天分类，超行分		类功能，可以根据实际情况分别使用同步和异步写入两种方式。其中异步写入方式，将生产者-消费者模型封装为阻		塞队列，创建一个写线程，工作线程将要写的内容push进队列，写线程从队列中取出内容，写入日志文件。

___

### sql_connection_pool.h 头文件涉及函数概要

T：MYSQL

### 包含的功能：

1、创建数据库连接池的单例模式

2、创建一个获取数据库连接的方法

3、创建一个链表变量存储数据库连接池资源

4、利用RAII机制对数据库连接的获取与释放进行封装，即通过创建RAII类，调用它的内部函数实现连接资源的获取和自		动释放

### 设计总结：

我们使用单例模式和链表创建数据库连接池，实现对数据库连接资源的复用

将数据库连接的获取与释放通过RAII机制封装，避免手动释放

不直接调用获取和释放连接的接口，将数据库连接的获取与释放封装起来，通过RAII机制进行获取和释放

___

### sql_connection_pool.cpp 文件涉及函数概要

H：mysql_init()、mysql_real_connect()、mysql_close()

### 包含的功能：

1、实现通过静态方法内的局部静态变量获取到连接池对象的唯一实例

2、初始化数据库连接池。内部调用mysql的API，使用初始化数据库连接变量方法，使用连接数据库的方法，将连接上的		资源入到数据库连接池队列，统计累计创建出来的空闲连接数。再使用信号量初始化自身为最大连接次数，以此监控		者和消费者对资源的获取情况

3、实现获取数据库连接的方法。使用信号量阻塞等待，同时还要使用互斥锁完成多线程操作连接池的同步操作。当有请		求时，从数据库连接池队列中取出一个可用连接，将其中队列中删除然后返回取出的连接，更新使用和空闲连接数

4、实现释放当前数据库连接的方法（注意不是删除连接）。将传进来的连接重新加入到连接池队列，并通过信号量通知		又产生了一个可用资源

5、实现销毁数据库连接池的方法。通过数据库连接池队列的迭代器对队列里面的每一个数据库连接进行关闭。然后清空		列表

6、实现在RAII的构造函数中，通过参数获取到的对象来调用对象获取一个数据库连接的方法，将获取到的接连变量和连		接池变量统一交由RAII内的变量管理(指针指向同一块内存)。最后通过RAII的析构函数实现对连接资源的释放。这样		就不用手动调用释放资源的方法了。实现手动取用RAII，自动释放RAII

### 设计总结：

本次使用单例模式和链表创建数据库连接池，实现对数据库连接资源的复用

系统需要频繁访问数据库时需要频繁创建和断开数据库连接，而创建数据库连接是一个很耗时的操作，也容易对数据库造		成安全隐患。在程序初始化的时候，集中创建多个数据库连接，生成数据库连接池，并把他们集中于此管理，供程序		使用，可以保证较快的数据库读写速度，更加安全可靠

___

### lst_timer.h 头文件涉及函数概要


T：sockaddr_in、time_t

### 包含的功能：

1、利用结构体创建一个封装起来的客户连接资源，包含套接字地址、文件描述符和定时器

2、创建定时器类，以带头尾结点的升序双向链表为该类的容器。创建前继后继定时器，同时创建一个回调函数等待调用		而管理客户连接资源

3、创建定时器容器类。创建增加、调整和删除定时器的方法以及定时任务处理的公有方法，但增加和调整都是通过调用		一个添加定时器的私有重载函数来调整定时器的链表内部处理的

4、创建一个工具类。创建对文件描述符的非阻塞设置、注册内核事件表的事件以及信号设置和处理的方法，再创建一个		定时处理任务的方法，不断触发SIGALRM信号。注意信号处理函数 static void sig_handler(int sig) 被设置为静态成		员函数是因为可以在不需要对象实例的情况下调用。信号处理函数需要在信号发生时调用，但是信号是异步事件，可		能会在任何时候发生，甚至在对象还没有创建或已销毁的情况下。所以将信号处理函数 sig_handler 设置为静态的原		因是为了兼容信号捕捉的机制

### 设计总结：

本项目中，服务器主循环为每一个连接创建一个定时器，并对每个连接进行定时。另外，利用升序时间链表容器将所有定		时器串联起来，若主循环接收到定时通知，则在链表中依次执行定时任务。

> 定时器，利用结构体将多种定时事件进行封装起来。这里只涉及一种定时事件，即定期检测非活跃连接，将该定		事件与客户连接资源封装为一个结构体定时器
>
> 定时器容器，我们使用升序链表将所有定时器串联组织起来

项目中使用的是SIGALRM信号的定时的方法。利用alarm函数周期性地触发SIGALRM信号，信号处理函数利用管道通知		主循环，主循环接收到该信号后对升序链表上所有定时器进行处理，若该段时间内没有交换数据，则将该连接关闭，		释放所占用的资源。

>三种定时方法：
>
>- socket选项SO_RECVTIMEO和SO_SNDTIMEO
>- SIGALRM信号
>- I/O复用系统调用的超时参数

___

### lst_timer.cpp 文件涉及函数概要

T：time_t、epoll_event、struct sigaction

>epoll_event这个结构体类型用来注册文件描述符上的事件，内部有events事件字段和另一个结构体类型data变量		的fd文件描述符字段

C：F_GETFL、F_SETFL、O_NONBLOCK、EPOLLIN、EPOLLET、EPOLLONESHOT、EPOLLRDHUP、                     				SA_RESTART

>EPOLLONESHOT为单次触发模式，在此模式下，一旦文件描述符上有事件到达并被处理，epoll会自动将该文件		描述符从就绪队列中移除，之后该文件描述符将不再被监听。如果希望继续监听该文件描述符上的事件，需		要重新将其加入到epoll的监听队列中。

H：time()、fcntl()、epoll_ctl()、assert()、send()、memset()、sigfillset()、sigaction()、alarm()

>assert函数运行时检查某个条件是否为真，如果为假（即条件不满足）则触发断言错误，终止程序的执行并打印		错误信息
>
>alarm函数设置信号传送闹钟，即用来设置信号SIGALRM在经过特定时间参数后发送给目前的进程。如果未设置		信号SIGALRM的处理函数，那么alarm()默认处理终止进程

### 包含的功能：

1、实现定时器容器类的构造函数和析构函数，注意析构函数即删除循环遍历并删除每一个链表定时器结点

2、实现公有添加定时器的方法。若当前链表中只有头尾结点，直接插入，此时头尾结点都是它；若此定时器超时时间小		于头结点，则将其更新为头结点；若上述都不是，则调用添加定时器的私有重载函数实现结点的位置调整

3、实现调整定时器的方法。任务发生变化时，调整已有定时器在链表中的位置。若被调整的目标定时器在尾部，或定时		器新的超时值仍然小于下一个定时器的超时，不用调整；若被调整定时器是链表头结点，先安排下一个结点为头节		点，将定时器取出重新插入，注意为调用添加定时器的私有重载函数实现结点的位置调整；若被调整定时器在内部，		将定时器取出重新插入，注意取出过程为双向链表的删除结点操作，然后添加定时器的私有重载函数实现被取出结点		与它后面的节点进行比较调整

4、实现定时器结点的删除。注意要删除的结点在头尾中部的不同情况

5、实现定时任务处理方法。获取当前时间戳，与从头循环定时器的超时时间进行比较，若当前时间还未到即小于定时器		的超时时间时，则不用处理，继续遍历；若检测到定时器到期，则调用这个定时器的回调函数并传入封装好的连接资		源去执行定时事件即释放资源，将处理后的定时器从链表容器中删除，并重置下一个结点称为头结点

6、实现私有重载实现定时器在链表内部即非头尾的真正调整函数。若非尾结点则循环比较被取出的定时器的超时时间与		链表头部或被取出的定时器的下一个结点被作为的头部 的下一个结点的超时时间进行比较，若小于头部的下一个结		点，则将其插入到此结点的前面。注意循环时，指针要迭代

7、实现定时器工具类的最小时间间隔的初始化

8、实现对文件描述符设置非阻塞的方法。设置完后返回旧的文件描述符的标志属性

9、实现将读事件在内核事件表的注册。先设定基础读和读端终端的事件，按情况再加上ET触发模式和EPOLLONESHOT		触发模式，将设定好的事件注册到内核事件表中。再将文件描述符设置为非阻塞

10、实现信号处理函数。为保证函数的可重入性，保留原来的错误号。然后按字符字节流的方式将数值信号转为单个字符			形式表达写入到管道的读端。将之前保存错误号重新设置给错误号变量。注意信号处理函数中仅仅发送信号值，不			做对应逻辑处理

11、实现一个信号设置的函数。函数接收信号处理函数作为参数，先创建一个用于设置信号处理的结构体变量，将其里面			可能的值都清空，将传进来的参数作为信号集的处理参数，并按情况设置使被信号打断的系统调用自动重新发起。			再将所有信号加入到阻塞信号集。然后对指定信号进行捕捉交给信号处理函数处理

12、实现定时器处理的函数。调用定时任务处理函数，将要执行的任务进行执行或删除某些过期任务。然后调用闹钟函数			开始计算下次定时器处理时间

13、实现定时器回调函数。从内核事件表中删除非活动连接，关闭文件描述符，释放客户连接资源。网页客户端连接数减			一

### 设计总结：

**信号通知流程**，Linux下的信号采用的异步处理机制，信号处理函数和当前进程是两条不同的执行路线。具体的，当进程		收到信号时，操作系统会中断进程当前的正常流程，转而进入信号处理函数执行操作，完成后再返回中断的地方继续		执行。	为避免信号竞态现象发生，信号处理期间系统不会再次触发它，即在信号设置函数中使用将所有信号加入到		阻塞信号集，通过信号捕捉函数交由信号处理函数处理。所以为确保该信号不被屏蔽太久，信号处理函数需要尽可		能快地执行完毕，即信号处理函数不进行逻辑上的处理，仅通过管道发送信号通知程序主循环，将信号对应的处理逻		辑放在程序主循环中，由主循环执行信号对应的逻辑代码

**统一事件源**，将信号事件与其他事件一样被处理。具体的，信号处理函数使用管道将信号传递给主循环，信号处理函数往		管道的写端写入信号值，主循环则从管道的读端读出信号值......

**信号处理机制**，内核态检测信号，用户态处理信号

- 信号的接收

- - 接收信号的任务是由内核代理的，当内核接收到信号后，会将其放到对应进程的信号队列中，同时向进程发送一个中断，使其陷入内核态。注意，此时信号还只是在队列中，对进程来说暂时是不知道有信号到来的

- 信号的检测

  - 进程陷入内核态后，有两种场景会对信号进行检测，即我们预设的闹钟信号和终止信号：

- - 进程从内核态返回到用户态前：进行信号检测
    - 进程在内核态中，从睡眠状态即阻塞状态被唤醒的时候进行信号检测
    - 当发现有新信号时，便会进入下一步，信号的处理

- 信号的处理

- - ( **内核** )信号处理函数是运行在用户态的，调用处理函数前，内核会将当前内核栈的内容备份拷贝到用户栈上，并且修改指令寄存器（eip）将其指向信号处理函数
  - ( **用户** )接下来进程返回到用户态中，执行相应的信号处理函数
  - ( **内核** )信号处理函数执行完成后，还需要返回内核态，检查是否还有其它信号未处理
  - ( **用户** )如果所有信号都处理完成，就会将内核栈恢复（从用户栈的备份拷贝回来），同时恢复指令寄存器（eip）将其指向中断前的运行位置，最后回到用户态继续执行进程

至此，一个完整的信号处理流程便结束了，如果同时有多个信号到达，上面的处理流程会在第2步和第3步骤间重复进行

**信号通知逻辑**

- 创建管道，其中管道写端写入信号值，管道读端通过I/O复用系统监测读事件
- 设置信号处理函数SIGALRM（时间到了触发）和SIGTERM（kill、Ctrl+C会触发）

___

### http_conn.h 头文件涉及函数概要

T：sockaddr_in、struct stat、struct iovec、map<>

### 包含的功能：

1、创建一个网页连接类。枚举出报文的请求方法，如GET、POST；枚举出主状态机的状态，如处理请求行、处理请求头		部、处理消息体的状态，标识解析位置；枚举出整个报文的解析结果；枚举出从状态机的状态，标识解析一行的读		取状态

2、......

___

### http_conn.cpp 头文件涉及函数概要

T：MYSQL、MYSQL_RES、MYSQL_FIELD、MYSQL_ROW、struct stat、

H：mysql_query()、mysql_error()、mysql_store_result()、mysql_num_fields()、mysql_fetch_fields()、		   				  			mysql_fetch_row()、memset()、recv()、strpbrk()、strspn()、strchr()、strlen()、strcat()、atol()、malloc()、			strncpy()、free()、stat()、open()、munmap()、writev()

C：EPOLLIN、EPOLLET、EPOLLONESHOT、EPOLL_CTL_ADD、EPOLL_CTL_MOD、S_IROTH

>若之前执行过一次SELECT查询，MySQL服务器会将查询结果缓存起来，然后可以：
>
>mysql_store_result函数来将这些结果从服务器检索到客户端，并存储在一个MYSQL_RES结构体中，提供动态		读取
>
>mysql_num_fields函数获取结果集中的字段数即列数
>
>mysql_fetch_fields函数获取查询结果中各个字段即列的元数据信息。它可以用于获取关于每个字段的详细信息，		如字段名、数据类型、长度等，并存储在一个MYSQL_FIELD结构体中，，提供动态读取
>
>mysql_fetch_row函数从结果集中获取下一行数据，以数组形式返回，数组的每个元素对应一列的数据值
>

### 包含的功能：

1、先定义一些HTTP响应信息。依据状态码设置相对应的描述信息

>常见状态码:
>
>- 1xx：指示信息--表示请求已接收，继续处理。
>
>- 2xx：成功--表示请求正常处理完毕。
>
>- - 200 OK：客户端请求被正常处理。
>  - 206 Partial content：客户端进行了范围请求。
>
>- 3xx：重定向--要完成请求必须进行更进一步的操作。
>
>- - 301 Moved Permanently：永久重定向，该资源已被永久移动到新位置，将来任何对该资源的访问都要使用本响应返回的若干个URI之一。
>  - 302 Found：临时重定向，请求的资源现在临时从不同的URI中获得。
>
>- 4xx：客户端错误--请求有语法错误，服务器无法处理请求。
>
>- - 400 Bad Request：请求报文存在语法错误。
>  - 403 Forbidden：请求被服务器拒绝。
>  - 404 Not Found：请求不存在，服务器上找不到请求的资源。
>
>- 5xx：服务器端错误--服务器处理请求出错。
>
>- - 500 Internal Server Error：服务器在执行请求时出现错误。

2、实现查询数据库表的表的方法。使用之前创建的数据库连接类RAII对MYSQL对象进行管理，从连接的数据库中的用户		表查询用户名和密码字段。将查询到的结果集保存到结果集结构体中。统计结果集中的字段的列数。获取到所有字段		的元数据并保存在一个结构体中。从结果集中读取每一行数据，结果保存在字符串数组，再按字段对应保存在字典容		器中

3、创建并实现一个设置非阻塞函数。对文件描述符设置非阻塞

4、创建并实现一个往内核事件表注册读事件的函数。ET模式，选择开启EPOLLONESHOT

>开启EPOLLONESHOT是因为 epoll模型的ET模式一般来说只触发一次，然而在并发程序中有特殊情况的存在，	譬如当epoll_wait已经检测到socket描述符fd1，并通知应用程序处理fd1的数据，那么处理过程中该fd1又有新	的数据可读，会唤醒其他线程对fd1进行操作，那么就出现了两个工作线程同时处理fd1的情况。应该再附加一	个EPOLLONESHOT事件加以避免

5、创建并实现一个删除内核事件表中被监听的文件描述符，并关闭该文件描述符

6、 创建并实现一个重置被监听的文件描述符的EPOLLONESHOT事件的方法。使用_MOD参数

7、实现关闭HTTP的方法。若套接字文件描述符有效，则调用删除内核事件表中被监听的文件描述符的方法，然后将这个		套接字设置为无效，并且客户数减一

8、实现初始化HTTP连接的一些需要数据的方法。将传进来的套接字文件描述符用注册读事件的方法注册到内核事件表		中，再将传进来的数据库信息传给成员变量进行保存，然后调用私有的初始化已接受的HTTP连接的一些默认值的方		法

9、实现初始化已接受的HTTP连接的一些默认值的方法。用它来初始化新接受的连接，对一些需要用到的成员变量都设置		一个默认值，将一些数组类型的成员变量的值也初始化为0

10、实现读取浏览器端发送来的请求报文的方法。先判断读缓冲区是否已满，满则返回读取失败。判断若是水平触发模			式，则接收套接字传来的数据并放入都缓冲区中，注意偏移放置和偏移后剩余的空间大小，读入后要更新指向数据			末尾的指针，未读到数据返回错误；若是边缘触发模式循环读取完全部数据，同样更新末尾指针，若出现错误号为			EAGAIN或EWOULDBLOCK表示已暂无数据到来，跳出循环函数，返回执行成功，如果信号接收函数的返回值为			0，则表示对方已关闭连接，直接返回执行失败

>注意EAGAIN与EWOULDBLOCK作用几乎相同，为提高移植性应同样方法一同处理它两
>
>ET边缘触发模式：线程通知有读写数据后就执行后面的代码，无需阻塞等待处理完才进行下一步操作

11、实现从状态机解析请求报文的每一行的方法。对整行数据进行遍历，记录每次遍历时遍历的当前字符，若当前字符为			\r，判断下一个字符是否已到行尾，若是则返回继续等待的标志，若下一个字符是\n则将\r\n置为\0\0同时返回整行			接收成功的标志，前面说的都不是则返回语法错误标志；若当前字符为\n(一般是上次读取到\r就到了全部报文的末			尾，没有接收完整，再次接收时会出现这种情况)，判断前一个字符是否为\r，若是则将\r和\n都置为\0\0同时返回			整行接收成功的标志，前面说的都不是则返回语法错误标志；前面说的都不是则返继续等待标志

> 在HTTP报文中，每一行的数据由\r\n作为结束字符，空行则是仅仅是字符\r\n
>
> 从状态机负责读取buffer中的数据，将每行数据末尾的\r\n置为\0\0
>
> 并更新从状态机在buffer中读取的位置m_checked_idx，以此来驱动主状态机解析

12、实现解析请求报文的请求行的方法。解析HTTP请求行，获得请求方法 目标url HTTP版本号。从传入的字符串中查询			第一个行内字段分隔符的位置，并返回位置指针，若一个分隔符都没有则返回请求报文的语法错误。然后先将分隔			符置为\0，后移动到下一个字符。然后使用忽略大小写字符串比较的判断被\0间隔部分的字符串是否为等于GET，			若是则将GET常见赋给指示请求方法的成员变量，POST请求同理设置，注意设置标记变量，两种请求都不是则返			回错误语法标记。结合函数偏移指针跳过可能还含有的分隔符寻找下一个字段，此时这个指针位置请求资源头部字			符位置上。我们新建一个要指向HTTP版本号的指针，利用上一个指针找到HTTP版本号前的分隔符的位置并传给			新指针，新指针为空返回语法错误，不为空则将这个分隔符置为\0分隔，并将其后移一个位置此时仍不一定为下一			个字段的头部，再将新指针跳过可能还有的分隔符，此时新指针指向HTTP版本号头部，加上此前从状态即已将请			求行的尾部的\r\n置为了\0\0，我们可以直接使用字符串操作了。利用这个指向HTTP上的新指针，判断版本号是否			为HTTP/1.1，若不是则返回语法错误标志。再回过头来利用仍指向请求资源上的指针和直线处理为\0结尾的设置			来读取请求资源的具体样式，具体的，通过指定长度比较字符串是否为http://这些前缀，利用指针偏去掉过这些部			分，再通过查到到第一/符跳过域名部分，指向了真正请求资源的路径。执行完上面的一系列操作后，如果存储url			的变量为空或者第一个字符不是/，则返回语法错误，如果是/，且变量的长度为1，即只有/，此时向该url拼接上欢			迎界面的HTML文件。最后将主状态机的的状态转为处理请求头部的状态标志，同时返回一个需要继续处理请求报			文数据的的标志。

> 在HTTP报文中，请求行和请求头部的各个字段之间通过 \t 或 空格 分隔
>
> 有些报文的请求资源中会带有http://或https://，需要将此前缀去掉
>
> 一个完整的HTTP URL为http:// localhost/path之类

13、实现解析请求报文的请求头以及处理空行的方法。先判断当前行是空行还是请求头，若是空行，再判断请求体是否有			请求数据，若有表示为POST请求，此时将主状态机的状态标志转为处理请求体数据的状态并返回继续读取请求报			文数据标志，若没有请求数据则为GET请求，返回已获取到完整HTTP请求的标志；否则如果通过指定字符串长度			比较的字段为键Connection:，则跳过键再跳过可能含有的分隔符，去到值的位置，此时判断是长连接还是短链接			进行指示长连接标志。同理处理Content-length:和Host:字段，前者将长度值设置到记录长度的成员变量处，后者			也将域名设置到记录域名的成员变量处。将不需要解析的行作错误提示处理。最后返回继续读取请求报文数据标			志

> 注意请求头部为许多行信息组成的，不指一个单独个体，每行内容基本为 [ 键：值\r\n ] 格式，此前从状态机已替	换\r\n为\0\0

14、实现解析请求报文的请求体数据的方法。先判断HTTP报文是否已经全部被完整读入，即判断整个报文占缓冲区的大			小是否大于等于前面解析的请求体数据的大小与目前缓冲区被读取到的位置(即已读完请求行、请求头部、空行处)			两者之和，大于则是已读取完整，此时将请求体的内容的结尾添加\0，然后将该内容串的地址赋给成员变量保存，			即此时已提取到请求体的内容，返回获得了完整的HTTP请求的标志。否则返回继续读取请求报文数据标志

15、实现有限状态机处理请求报文的方法，包括了从状态机和主状态机的结合使用。此方法根据解析结果HTTP_CODE，			进入相应的逻辑和模块。先初始化从状态机状态、HTTP请求解析结果。设置一个循环来处理报文，循环条件为：			如果是GET请求则条件设置为从状态机状态等于整行完整接收(每次循环调用一次从状态机解析整行，一次循环处			理一行)，如果是POST请求单从从状态机状态不能判断是否已接收完整，因为从状态机未能处理请求体，我们该			为以主状态机的状态作为循环入口条件，如果是消息体数据处理状态并且满足我们设置的一个检查标志则循环(循			环中，若处理完请求体数据则改变该检查标志，跳出循环)。先获取到有函数返回的已解析但未处理的字符的位			置，提前将指针指向下一次要处理的行的头部上，同时利用日志输出被处理的字符串。使用条件变量依据主状态机			的状态处理不同的请求部分，若是要处理请求行，则调用请求行解析函数，判断处理结果如果是语法错误则返回语			法错误标志，否则继续执行循环；若是处理请求头部，则调用请求头部解析函数，判断处理结果如果是获得了完整			的HTTP请求即报文是GET请求，则调用生成报文的函数进行操作；若是处理请求体的数据，判断处理结果如果是			获得了完整的HTTP请求，同样调用生成报文的函数进行操作，此时处理完应该更新从状态机的状态标志，避免再			次进入循环。若上述条件都不满足返回服务器内部错误标志(一般不会触发)。循环执行终止，请求不完整，返回需			要继续读取请求报文数据标志

16、实现报文响应的方法。先将服务器该项目的本地目录保存到存储读取文件的名称的数组内，然后找到url的/符的位			置。然后实现登录和注册校验，通过判断是否为POST请求以及指针指向的/的后一个数字是否为校验需求进入次功			能，先保存着这个数字位，然后创建一个大小为200字节的动态分配字符串数组，将url的/以及后面的资源名保存			到该数组，再将数组的资源名复制到已保存本地目录的数组内，注意先偏移再复制形成拼接效果以及改拷贝的长度			应该是剩余空间的最大长达以达到处理两种不同校验时长度不同的url的效果，即利用到拷贝函数的指定能用容量			大小的功能，拼接完要释放动态分配的数组。对之前解析到并保存了的请求体数据进行读取，我们这里的数据为表			单提交的账号密码字段信息，我们先跳过user=这5个字符然后一直遍历并保存到储存用户名的数组中，并将结尾			置\0。接下来判断，如果是注册检验，将用户名和密码数据编排称为一句SQL插入语句，放入到动态字符串数组中			备用，然后通过遍历判断之前已将数据库查询并保存在map容器内的用户名键是否相等即重名，注意此过程要使用			互斥锁保持线程同步，若不相等则可将其使用刚刚保存的插入SQL语句对数据库进行插入操作，同时将该用户信息			用map容器的插入操作结合对组进行map容器的记录更新，然后对SQL操作的返回值进行判断，若执行成功则将url			替换为登录界面文件名等待后续网页跳转操作，若执行失败则将url替换为注册错误界面文件名；否则如果是登录			检验，若直接在map容器中能找到该用户名，并且输入的用户名与密码符合map的对应关系，则将url替换为欢迎界			面文件名，否则将url替换为登录错误界面文件名。接下来处理其他资源请求，如果请求资源为/0，创建一个大小为			200字节的动态分配字符串数组，将目标注册界面文件名资源文件保存于此，然后将其以指定长度偏移拷贝的方式			安全地拼接到项目的本地目录上，然后释放此前的动态数组。否则为其他资源，同理处理/1或/5或/6或/7资源。若			以上资源资源都不符合，此时请求资源仅为/，将此资源拼接到项目根目录，即为欢迎界面。然后通过判断拼接好			的目标资源的文件属性信息是否存在来判断资源文件是否存在，若存在则将属性信息保存到一个结构体成员变量			中，若不存在则返回请求资源不存在的报文解析标志。通过比较文件属性信息结构体内的一个mode变量与一个宏			S_IROTH按位与操作判断网页用户是否有读权限，若为读权限4的布尔值反数即0无该权限，则返回请求资源禁止			访问报文解析标志。然后依然通过mode变量排除请求资源为目录的情况，若是目录则返回请求报文有语法错误标			志。最后以只读方式打开文件并获取文件的文件描述符，通过mmap将该文件映射到内存中，注意返回值为文件映			射在内存中的首地址，映射时指定被映射的目标资源文件的大小属性，映射时的保护级别为只读，以及映射区域设			置为私有映射而对该映射文件操作不会影响原文件，同时提供指向被映射的文件的描述符进行文件的指定。若前面			的设定均满足则返回请求资源可以正常访问的报文解析标志

>如请求体内数据信息为user=root&password=123456

17、实现取消内存映射的方法。判断如果指向目标资源文件映射在内存上的地址指针不为空，则使用取消映射函数其参数			指定这个地址以及目标资源文件的大小，然后将该指针置空

18、实现添加响应报文的公共函数方法，接收的参数为可变参数列表类型。判断如果写入内容超出写缓冲区大小则返回添			加失败。然后定义一个可变参数列表变量，将变量初始化为传入的可变参数列表的值，将可变参数列表格式化输出			并写入到缓冲区，返回写入数据的长度，判断若该长度大于缓冲区的剩余长度，则先清理可变参数列表变量，同时			返回添加响应报文失败，若不大于，则更新指示写缓冲区中数据的最后一个字节的下一个位置，然后清理可变参数			列表变量，将此次写入到缓冲区的数据添加到日志生成函数中，返回响应报文添加成功

19、实现向响应报文添加状态行的方法，需要提供状态码和状态消息这两个参数。内部调用添加响应报文的公共函数，为			该函数传入由HTTP协议版本号， 状态码， 状态消息这三部分组成可变参数列表，注意格式为字段间以空格分隔			以及行末\r\n，将该公共函数作为返回值，判断状态行是否添加成功

20、实现向响应报文添加消息报头的方法，需要提供文本长度这个参数。内部同时调用组成消息包头的各行的函数，主要			为文本长度行、文本类型行、连接状态行和空行的添加函数，将参数传给条件文本长度行的函数，这几个函数共同			做返回值，判断消息包头是否添加成功

21、实现向响应报文的消息包头添加文本长度行的方法，需要提供文本长度这个参数。内部调用添加响应报文的公共函			数，该函数传入长度变量组成可变参数列表，格式为键：值\r\n

22、实现向响应报文的消息包头添加文本类型行的方法。设置同上

23、实现向响应报文的消息包头添加连接状态行的方法。设置同上

24、实现向响应报文的消息包头添加空行的方法。注意写法为add_response("%s", "\r\n")

25、实现向响应报文添加响应正文文本的方法。需要提供文本的char指针类型的地址这个参数

26、实现生成响应报文的方法。依据报文解析的状态码结果进行条件条件判断。如果是服务器内部错误状态码，调用添加			状态行的函数传递500状态码和描述标题，调用添加消息包头的函数传递响应正文的大小，调用添加响应正文的函			数传递参数为该描述标题对应的详细描述，同时判断该响应正文是否添加成功，否则立即返回生成响应报文失败，			若三个调用都添加成功则跳出条件判断；如果是请求报文有语法错误状态码404，同上设置，若三个调用都添加成			功则跳出条件判断；如果是没有读取权限错误状态码403，同上设置，若三个调用都添加成功则跳出条件判断；如			果是请求资源可以正常访问状态码200，先添加状态行，判断请求资源是否有数据，若有数据，继续添加消息包			头，参数大小为文件属性大小，然后第一个iovec指针指向响应报文缓冲区，长度为缓冲区内数据的大小，第二个			iovec指针指向mmap返回的目标资源文件指针，长度为文件属性的大小，iovec指针数统计为两个，设置待发送的			全部数据的成员变量为响应报文头部信息和文件大小，设置完成返回生成响应报文成功；否则，如果请求的资源大			小为0，则返回自定义的一个字符串数组保存一个空白html文件，调用添加消息包头的函数传递此字符数组的字符			个数大小，调用添加响应正文的函数传递参数为该该字符串数组，同时判断该响应正文是否添加成功，若最后一个			添加错误返回生成响应报文失败，若成功则跳出条件判断；条件判断的默认值为返回生成响应报文失败。除			FILE_REQUEST且目标资源有大小状态能申请两个iovec指针，其余状态只申请一个iovec，iovec指针指向响应报			文缓冲区，长度为缓冲区内数据的大小，iovec指针数统计为一个设置待发送的全部数据的成员变量为响应报文头			部信息，最后返回响应报文添加成功

27、实现处理http报文请求与报文响应的方法。调用有限状态机处理处理请求报文的方法，判断返回的报文解析结果，如			果为需要继续读取请求报文数据，则重新为套接字文件描述符注册EPOLLONESHOT事件并监测读事件，并且结			束当前函数。然后调用生成响应报文报文的方法，参数为有限状态机的报文解析结果，如果返回值为false表示生			成失败，执行连接关闭函数，如果返回值为true表示生成成功，则重新为套接字文件描述符注册				          			EPOLLONESHOT事件并监测写事件

28、实现将响应报文发送给浏览器端的方法。先判断如果响应报文的数据为空这种较少概率情况，则重新为套接字文件描			述符注册EPOLLONESHOT事件并监测读事件，使其只能被一个线程使用，然后调用初始化已接受的HTTP连接的			一些默认值的方法，等待该套接字重新写入数据，返回能继续发送数据提示。接下来使用循环将响应报文的状态			行、消息头、空行和响应正文发送给浏览器端，具体的，使用writev函数向浏览器端发送组成完整响应报文多个非			连续缓冲区的数据(聚集写，包括响应报文头部信息和目标文件资源在内存中的映射资源)，如果发送错误，判断是			否是因为写缓冲区是否已满，若是则重新为套接字文件描述符注册EPOLLONESHOT事件并监测写事件，等待下			一次发送的机会，同时返回true表示能继续发送数据，如不是缓冲区问题，则直接取消目标文件资源内存映射，返			回不能继续发送数据，若发送完成，更新已发送字节数和未发送字节数。接下来，需要更新iovec结构体中的指针			和长度使writev函数能够循环执行，判断如果已发送的字节数大于等于第一个iovec的长度，则将这个长度置0，表			示下次循环不再继续发送第一个iovec的数据，然后我们指定第二个要被发送的iovec的指针指向目标支援文件在内			存中的地址，按情况偏移已发送字节数减去响应报文头部信息的长度，长度为剩余发送字节数；如果已发送的字节			数小于第一个iovec的长度，指定第一个要被继续发送的iovec的指针指向写缓冲区地址并偏移已发送字节数，长度			为原来缓冲区数据大小减去已发送的数据长度。然后判断剩余数据还有没有，若有则继续循环执行writev聚集写，			若没了即表示数据已全部发送完，则先取消内存映射，在epoll树上为该文件描述符重置EPOLLONESHOT事件并			监听写事件，判断如果是请求为长连接，则初始化该套接字的一些基础设定，返回能继续发送数据，否则返回不能			继续发送数据

>  循环调用writev函数时，需要重新处理iovec结构体中的指针和长度，因为该函数不会对这两个成员做任何处理

### 设计总结：

**有限状态机**，有限状态机，是一种抽象的理论模型，它能够把有限个变量描述的状态变化过程，以可构造可验证的方式呈	现出来。有限状态机可以通过if-else,switch-case和函数指针来实现，主要是为了封装逻辑。有限状态机一种逻辑单元内	部的一种高效编程方法，在服务器编程中，服务器可以根据不同状态或者消息类型进行相应的处理逻辑，使得程序逻辑	清晰易懂

**iovec机制**，ovec（I/O向量）是一种在进行I/O操作时用于传递多个缓冲区和长度的机制。它通常用于提高数据传输的效率，特别是在需要传输多个非连续的数据块时。iovec结构体包含了指向不同缓冲区的指针和每个缓冲区的长度。

> iovec向量机的作用主要有以下几个方面：
>
> 1. **减少数据拷贝：** 在传统的I/O操作中，数据需要从应用程序缓冲区拷贝到内核缓冲区，然后再拷贝到目标设备。使用iovec向量机制，可以将应用程序的多个缓冲区以及长度信息直接传递给内核，从而减少了不必要的数据拷贝，提高了数据传输效率。
> 2. **支持分散/聚集操作：** 分散（Scatter）操作是指将一个缓冲区的数据分散写入多个目标缓冲区，聚集（Gather）操作是指从多个源缓冲区中聚集数据到一个目标缓冲区。iovec向量机制支持这种操作，使得在一次I/O操作中可以操作多个缓冲区，适用于非连续数据的传输。
> 3. **提高性能：** 由于iovec向量机制避免了不必要的数据拷贝，减少了数据在内核空间和用户空间之间的复制，从而提高了数据传输的性能和效率。
> 4. **内存映射（mmap）：** 在内存映射的操作中，iovec向量机制可以方便地将多个内存区域映射到文件的不同部分，从而实现高效的文件读写。

实现对数据库的一些简单查询，结果备用

使用LT或ET模式，recv接收来自浏览器端的数据，写入到读缓冲区中

使用有限状态机实现对报文的解析。首先利用从状态机解析从读读缓冲区中识别出每一行数据，主状态机依据从状态的结	果对状态进行切换，依次处理请求行，请求头部...，解析出请求报文的每个部分的每行信息并跳转到下一个部分。依据	GET或POST请求调	用完成报文的响应

具体的响应方法是通过主状态从请求行解析到的url判断需要跳转到哪个html文件，并将文件名与项目的根目录进行拼接，	形成一个完整的请求资源路径，注册和登录界面都需要之前备用的数据库信息。如果请求资源为文件而不是一个目录，	则判断文件的属性，然后将文件映射到内存上，等待数据传输

实现一个添加响应报文的公共函数来组成每行的格式，使添加响应报文状态行、消息报头、空行和消息体各行的函数都调	用这个公共函数，一齐写入到写缓冲区。注意此公共函数的参数为课表参数列表，接收每行的不同参数值

实现一个生成完整响应报文的函数，依据有限专状态机对报文的解析结果，进行不同的响应报文生成，请求失败或资源为	空的都只使用一个iovec指针指向写缓冲区，请求成功且目标资源文件有大小的则使用两个iovec指针，第二个指针指向	资源文件映射在内存中的地址，并指定好这个完整数据的各个地址

实现一个将响应文发给浏览器端的函数，注意返回结果为能否继续发送数据。通过循环，使用writev函数结合iovec机制，	实现多个缓冲区的聚集写，内容写给套接字，实现了完整的报文响应过程

___

### webserver.h 头文件涉及函数概要、

1、先设定好最大文件描述符、最大事件数、最小超时时间这三个整形常量

2、创建一个服务器类。内部创建http连接、数据库连接池、线程连接池的对象等一些基本成员

......

____

### webserver.cpp 文件涉及函数概要

T：struct linger、struct sockaddr_in、socklen _t、struct sockaddr*

C：PF_INET、SOCK_STREAM、AF_INET、SOL_SOCKET、SO _REUSEADDR、PF_UNIX、SIGPIPE、SIG_IGN、		SIGALRM、SIGTERM、EINTR、EPOLLRDHUP、EPOLLHUP、EPOLLERR

H：getcwd()、socket()、setsockopt()、bzero()、htons()、htonl()、bind()、epoll_create()、sockpair()、alarm()、		 		accept()、epoll_wait()

> bzero 函数会将内存块（字符串）的前指定字节清零，在网络编程中会经常用到
>
> bind 函数将指定的地址和端口与监听套接字关联，使得该套接字可以监听并接受来自指定地址和端口的连接请	  	求。具体来，说服务器需要绑定一个固定的IP地址和端口号，以便客户端可以通过这个地址和端口号找到服务	器并建立连接。服务器可以选择绑定特定的IP地址，也可以使用通配符绑定所有可用的IP地址。端口号通常选	择未被占用的合法端口
>
> listen 函数的第二个参数为等待连接队列的最大长度，此参数的设置会影响到服务器能够同时处理的并发连接	 	数。其实这个的意思是说，在某一时刻(注意，是某一时刻)同时允许最多有backlog个客户端要和服务器端进行	连接，而不是像有些人想的那样，只能允许backlog个服务端与客户端进行连接。此等待连接队列主要用于存	放已经完成三次握手但尚未被accept函数接受的连接。一旦服务器调用了accept函数来接受一个连接，该连接	就会从等待连接队列中移出，并交由服务器进程处理
>
> socketpair 函数用于创建一对无名的、相互连接的套接子。创建好的套接字分别是pipefd[0]和pipefd[1]。这对套	接字可以用于全双工通信，每一个套接字既可以读也可以写。例如，可以往pipefd[0]中写，从pipefd1]中读，	或者从pipefd[1]中写，从pipefd[0]中读。如果往一个套接字(如pipefd[0])中写入后，再从该套接字读时会阻塞，	只能在另一个套接字中(pipefd[1])上读成功。读、写操作可以位于同一个进程，也可以分别位于不同的进程，	如父子进程。如果是父子进程时，一般会功能分离，一个进程用来读，一个用来写。因为文件描述符pipefd[0]	和pipefd[1]是进程共享的，所以读的进程要关闭写描述符，反之，写的进程关闭读描述符

### 包含的功能：

1、实现服务器类对象创建的构造函数。先通过动态数组创建http连接对象。获取到root文件资源的绝对路径，通过先获取		到服务器的当前工作目录，将其拷贝到一个字符数组中，然后与root这几个字符做拼接，保存在成员变量中。然后使		用动态数组创建含有定时器的客户连接资源连接资源

2、实现服务器资源释放的析构函数。关闭epoll文件描述符、监听文件描述符、管道两端文件描述符。删除构造函数中创		建的对象数组资源以及线程池资源

3、实现初始化服务器的端口号、数据库、日志类型、触发模式、数据库连接池和线程池数量、网络并发模型等成员变量		的方法

4、实现对I/O复用的epoll对象的文件描述符的事件触发模式设置的函数。判断若是模式0，设置监听文件描述符的事件触		发模式为LT、连接文件描述符的是事件触发模式为LT；若是模式1，设置监听文件描述符的事件触发模式为LT、连接		文件描述符的是事件触发模式为ET；若是模式2，设置监听文件描述符的事件触发模式为ET、连接文件描述符的是事		件触发模式为LT；若是模式3，设置监听文件描述符的事件触发模式为ET、连接文件描述符的是事件触发模式为ET

5、实现初始化日志系统函数。判断日系系统是否开启，若开启则判断日志类型，若是异步日志，通过公有静态方法获取		日志类的单例模式的实例，调用日志初始化函数，进行参数设置，同时指定最大阻塞队列长度；若是同步日式，设置		同上，当指定最大阻塞队列为0，即能设置为同步日志

6、实现初始化数据库连接池函数。通过公有静态方法获取数据库连接池类的单例模式的实例，调用数据库连接池初始化		函数，进行参数设置。然后通过http实例对象调用读取数据库表函数结果待用

7、实现创建线程池对象的函数。通过new关键字对线程池模板类创建一个对象，注意模板参数类型指定为http_conn

8、实现创建事件监听，包括网络套接字连接，Socket网络编程基础步骤实现，以及信号量的监听函数。先用sockt函数创		建一个用于监听的套接字，参数协议族为支持ipv4的协议族，协议类型为流式协议，即TCP默认使用的类型。判断是		否延时关闭该监听套接字，若不是则先创建一个延时参数		 		 SO_LINGER选项的结构体，然后使用设置套接		字属性的函数进行该功能不开启设置；若是延时关闭则同样创建这样的一个结构体，传入到设置套接字属性的函数		中。	创建一个sockaddr_in结构体类型的地址变量，使用bzero函数将该地址变量的内存的字节置0。然后设定连接		的端口和地址设定等信息，设置它的地址族为IPv4的AF_INET，设置端口号为将主机字节序转化为网络字节序，设置		ip地址同样为将主机字节序转化为网络字节序并设置为能接受任何地址的连接。	将监听套接字添加地址复用属性，		使用bind函数将之前的地址变量与监听套接字进行绑定，使其能被客户端识别并建立连接。	使用listen函数监听创		建的监听套接字，指定等待队列参数的最大值，即同一时刻的监听并发数最大值。通过工具类对象设置服务器的最小		时间间隙，使用epoll创建内核事件表，使用工具类对象调用方法为监听套接字注册读事件，默认不开启		 		 		EPOLLONESHOT，将设置好的内核事件表提供给http连接类成员，使得它能够使用该内核事件表对套接字注册监听		事件。创建管道套接字，参数套接字协议族设置为PF_UNIX，套接字类型设置为流式报文，协议使用0默认为TCP协		议，还需要传进去一个大小为2的整形数组，接收创建出来的两个管道套接字。然后将管道的写端(文件描述符)设置		为非阻塞，用于写入信号量，通过工具类对象，将管道读端(文件描述符)添加到内核注册表上，实现I/O复用系统监测		读事件，从而形成了统一事件源，该事件同其他文件描述符都可以通过epoll来监测。通过工具类调用设置信号函数来		处理三个信号，分别为添加捕捉SIGPIPE信号并忽略因管道问题停止程序和添加对SIGALRM、SIGTERM信号的捕捉		而交给信号集结构体变量内的信号处理函数去处理，注意此时信号将被信号处理函数使用send函数将信号量发送给		主循环(然后，主循环再根据接收到的信号值执行目标信号对应的逻辑代码)。然后就能开始倒计时发送ALARM信号。		再将管道套接字和内核时间表对象赋给工具类的两个静态成员变量

> 延时关闭（Linger）是一种网络编程中的一项机制，它允许操作系统在关闭套接字时等待一段时间，以确保未发	送的数据能够完全发送给对端，或者在一定时间内收到对端的确认
>
> struct sockaddr是通用的套接字地址，而struct sockaddr_in则是internet环境下套接字的地址形式，二者长度一	   	样，都是16个字节。二者是并列结构，指向sockaddr_in结构的指针也可以指向sockaddr。注意：struct 		 	sockaddr_in只适用于IPv4，struct sockaddr_in使用起来也更方便。一般情况下，需要把sockaddr_in结构强制	转换成sockaddr结构再传入系统调用函数中
>
> 网络字节序为大端(Big-Endian，对大部分网络传输协议而言)传输
>
> SO_REUSEADDR是一个套接字选项，用于控制是否允许在绑定套接字时重用处于 TIME_WAIT 状态的本地地	 	址。通常情况下，当一个 TCP 连接关闭时，它会进入 TIME_WAIT 状态一段时间。在这个状态下，操作系统	会保留连接的本地地址和端口，以确保任何延迟的数据可以被正确传递到目的地。但是，有时候在程序关闭	 	后，由于网络堆积、负载均衡等原因，可能需要在短时间内重新启动程序并绑定相同的本地地址和端口。这时	就可以使用 SO_REUSEADDR选项来允许重用处于 TIME_WAIT 状态的本地地址
>
> 管道写端m_pipefd[1]设置非阻塞：send是将信息发送给套接字缓冲区，如果缓冲区满了，则会阻塞，这时候会进	一步增加信号处理函数的执行时间，为此，将其修改为非阻塞。没有对非阻塞返回值处理，意味着会出现定时	事件失效的情况，但定时事件是非必须立即处理的事件，可以允许这样的情况发生
>
> 将 SIGPIPE 信号的处理函数设置为SIG_IGN，即忽略该信号。SIGPIPE 信号通常在进程向一个已经关闭的写端	的管道或套接字发送数据时触发。通过设置为SIG_IGN，可以避免因为不处理 SIGPIPE 信号而导致进程被终	止。这通常在网络编程中很常见，因为如果不处理 SIGPIPE 信号，进程在尝试向已经关闭的连接写数据时会	收到该信号并终止

9、实现单个定时器创建的函数，参数传递http连接对象的文件描述符和sockaddr_in结构体类型的地址变量。当有连接对		象进来时，每一个连接对象通过调用自己的初始化HTTP连接的一些需要数据的函数，将之前过获取到的全部成员变		量作为参数，实现连接的初始化，注意里面较重要的连接套接字的触发模式成员变量，为控制着报文读写事件的套接		字。将传入进来的结构体地址变量赋给连接资源对象的地址成员，同理设置连接的对象文件描述符。使用new函数对		定时器类创建一个定时器结点对象，然后为该定时器对象结点的成员赋上刚刚设定的客户连接资源对象，再为其回调		函数成员变量设置类外定义的回调函数。获取当前时间，此基础上加3倍最小超时初始化给定时器结点对象的超时时		间，然后又将该该定时器结对象结点赋给连接资源对象(此过程存在循环依赖)，最后将该定时器结对象结点假如到定		时器双向链表容器当中

10、实现调整定时器在链表上的位置的函数，参数传递定时器对象。若数据活跃，则使用该函数演唱定时器的超时时间。			先获取到当前时间，与其加上3倍最小超时时间给定时器的超时成员变量，调整后使用日志系统打印日志

11、实现删除定时器结点并关闭连接的函数，参数传递定时器对象和套接字文件描述符。定时器类对象调用定时器回调函			数，参数传递连接资源对象，执行从内核事件表删除事件，关闭文件描述符，释放连接资源操作。判断被删除的定			时器结点指针对象存在，然后通过工具类对象的成员定时器容器类对象的删除定时器成员函数删除该定时器结点，			参数传递定时器结点对象。删除后使用日志系统打印日志

12、实现接收处理客户端的函数。先创建一个用户接收客户端地址信息的机构体变量地址参数以及一个计算该地址长度			socklen_t类型的变量。判断监听套接字的事件的触发模式，若为LT水平触发，则使用 accept 函数单次接收从监听			套接字上监听到的客户端连接请求，返回值保存为连接套接字文件描述符，再判断该连接套接字是否为-1，若是则			代表接收错误，使用日志错误系统打印日志，返回接收处理客户端失败，判断http类中的静态成员变量客户连接数			是否已大于等于最大文件描述符个数，若是则通过工具类对象调用错误信息发送函数，将错误信息文本发送给参数			连接套接字的客户端，同时使用日志错误系统打印日志，返回接收处理客户端失败，都未发生错误，则将该连接套			接字连同客户端地址信息传给单个定时器创建的函数，定时器类通过这些信息创建一个定时器对象结点，通过工具			类升序链表定时器容器成员对象的定时器容器类对象的添加定时器函数将结点添加到升序链表中，等待信号量到			来，并取出删除资源；若为ET边沿触发，则使用while循环一直使用 accept 函数循环接收从监听套接字上监听到			的客户端连接请求，返回值保存为连接套接字文件描述符，再判断该连接套接字是否为-1，若是则代表接收错误，			使用日志错误系统打印日志，同时使用break跳出while循环，判断http类中的静态成员变量客户连接数是否已大于			等于最大文件描述符个数，若是则则通过工具类对象调用错误信息发送函数，将错误信息文本发送给参数连接套接			字的客户端，同时使用日志错误系统打印日志，同时使用break跳出while循环，都未发生错误，则将该连接套接字			连同客户端地址信息传给单个定时器创建的函数，定时器类通过这些信息创建一个定时器对象结点，通过工具类升			序链表定时器容器成员对象的定时器容器类对象的添加定时器函数将结点添加到升序链表中，等待信号量到来，并			取出删除资源，若while循环发生错误，无论是已接受完还是真正发生错误，跳出循环后立即返回接收处理客户端			失败。注意，只有单次接收成功才返回接收处理客户端执行，不代表循环接收都没接收连接，无论如何，在未出错			情况下，能被接受的连接请求早已被初始化连接信息并且加入了定时器链表容器

> 不同场景下的ET边缘触发模式读取完数据后返回错误的处理方案：
>
> 针对连接接受操作的代码段：
>
> - 使用 accept函数尝试接受客户端连接。
> - 如果accept返回 -1，则可能发生错误，需要根据错误码进行相应的错误处理，并且停止接受连接。
> - 如果已经达到了最大连接数MAX_FD，则无法继续接受连接，需要进行错误处理。
> - 否则，接受成功，创建一个新的客户端连接，调用timer函数来处理连接。
>
> 针对数据读取操作的代码段：
>
> - 通过recv函数尝试从套接字中读取数据。
> - 当 recv返回 -1 时，检查错误码，如果错误码是 EAGAIN（或 EWOULDBLOCK），这表示暂时没有数据可读，这是非阻塞套接字的一种预期情况，因此可以退出循环等待下一次读取。
> - 如果 recv返回 0，这表示连接被关闭，需要返回 false。
> - 如果recv返回正数（读取到数据的字节数），则将数据添加到缓冲区，继续循环读取。

13、实现处理信号的函数，参数使用引用地址传递定时处理函数功能是否开启执行是否开启、服务器是否关闭。使用recv			函数对套接字管道读端读取信号量，将信号量保存在数组中，同时保存recv操作的结果，判断此结果如果等于-1，			表示读取信号量错误，返回处理信号失败，如果等于0，表示管道写端关闭，通用返回处理信号失败，如果都没问			题，即结果大于0(正常情况下，此结果返回值总是1，只有14或15两个ASCII码对应的字符)，通过循环结果个数的			次数，进行switch条件判断接收到的信号量字符的ASCII码与SIGALRM和SIGTERM中哪个信号量值对应对应，如			果与SIGALRM信号对应上，则定时处理函数功能是否开启执行变量设置为开启，跳出循环，如果与SIGTERM信			号对应上，则服务器是否关闭设置为关闭，跳出循环。跳出循环后返回信号处理成功

14、实现处理客户连接上接收到的数据的函数，参数传递套接字文件描述符。创建定时器对象临时结点，将客户连接资源			中对应的定时器取出来，判断若处理读写IO事件的模式为reactor模式，整个读事件就绪的请求放在请求队列上，			判断如果刚创建的定时器对象临时结点不为空，则调用对新添加的定时器在链表上的位置进行调整的函数将此结点			往后延迟3个时间单位。通过线程池相关且指定参数模板为http_conn类型的对象，调用在reactor模式下向请求队			列中添加任务的函数将该单个http连接对象添加到请求队列中，并标记IO事件类型为读事件(后续，通过信号量通			知有任务能够处理，等待被后续执行)。然后通过while循环，判断http_conn类的当前对象是否正在被处理数据，如			果是再判断http是否关闭连接，若未关闭则调用删除定时器结点的函数释放资源同时删除定时器结点，更新连接关			闭标志，以及数据不在处理中，然后跳出循环；	若处理读写IO事件的模式为proactor模式，先读取数据再放进请			求队列，通过判断单个http连接对象的读取浏览器端发来的全部数据成员函数，若读取完成，则通过日志通知系统			打印连接客户端的地址调用http类的获取地址函数进行打印，然后调用在proactor模式下向请求队列中添加任务的			函数将该单个http连接对象添加到请求队列中,判断如果刚创建的定时器对象临时结点不为空，则调用对新添加的定			时器在链表上的位置进行调整的函数将此结点往后延迟3个时间单位；若先读取数据失败，则调用删除定时器结点			的函数释放资源同时删除定时器结点

15、实现向客户连接写入数据数据的函数，参数传递套接字文件描述符。流程大致如上，主要差别在：reactor模式下，将			该连请求接放入请求队列时，并标记IO事件类型改为写事件，proactor模式下，写入数据时，因为异步处理，且没			有等待连接过程即不再需要放进请求队列

> **读写事件的proactor与reactor模式的对比：**
>
> **写事件：**
>
> 请求队列通常是一个用于存放需要处理的任务的队列，主要用于实现任务的异步处理。请求队列可以理解为存放	需要处理的客户端请求的队列。当有新的客户端连接建立或者客户端有数据要发送或接收时，将对应的任务添	加到请求队列中，然后由线程池中的线程进行异步处理
>
> 在Proactor模式中，由于I/O操作是异步的，主线程会将读写操作交由操作系统处理(主线程和内核负责处理读写数	据)，而不需要等待操作完成。当主线程发起一个写操作后，它可以继续处理其他事件，而不必等待写操作完	成。因此，proactor模式中的写操作是非阻塞的
>
> 这就解释了为什么在Proactor模式下，写数据发给客户端时不需要将操作添加到请求队列中。因为写操作不会阻	塞主线程，主线程可以继续处理其他事件，不需要等待写操作完成，也不需要将写操作放入请求队列中
>
> 在Reactor模式中，写操作是在主线程中同步进行的，需要等待写操作完成后才能继续处理其他事件。所以，写	数据操作需要放入请求队列中，以便主线程能够顺序地处理每个请求，包括写操作
>
> 总之，Proactor模式下的写操作是由操作系统异步处理的，不需要主线程阻塞，所以不需要将写操作添加到请求	队列中。而Reactor模式下的写操作是在主线程中同步进行的，需要等待写操作完成，因此需要将写操作添加	到请求队列中，以保证顺序处理
>
> **读事件：**
>
> 无论是Reactor模式还是Proactor模式，读取数据操作都是涉及到从客户端接收数据，然后对这些数据进行处理。	在多线程服务器中，为了实现高并发处理，通常会使用线程池来异步处理客户端请求
>
> 无论是哪种模式，主线程在接收到客户端数据后，都可以将处理该请求的任务添加到请求队列中，然后由线程池	中的线程来处理这些任务。这样可以实现请求的异步处理，提高了服务器的并发能力和响应速度
>
> 在Reactor模式下，由于主线程需要同步地处理读取数据的操作，将任务添加到请求队列中是为了确保主线程按	顺序处理每个请求，不会造成数据的混乱和交叉
>
> 在Proactor模式下，虽然读取数据的操作是由操作系统异步处理的，但是主线程同样可以将任务添加到请求队列	中，这样可以更好地管理和调度所有请求的处理，确保服务器能够高效地处理多个客户端连接
>
> 总之，无论是哪种模式，将读取数据的任务添加到请求队列中都是为了实现请求的异步处理，从而提高服务器的	并发性能和响应速度

16、实现服务器主线程函数。先创建连个变量记录定时处理函数功能是否开启执行以及服务器是否关闭，两者都初始化为			false。若服务器未关闭作为条件而一直while循环，使用epoll_wait函数阻塞等待监听epoll树上的被监听的文件描述			符的事件发生，将监听到的文件描述符和对应的事件保存在对应类型的有就绪事件数组events中，提供后续判断依			据，函数的返回结果保存在一个变量中，判断如果结果小于0即检测失败且错误号不是EINTR，则使用日志错误系			统打印日志，同时跳出循环。未出错情况下，按照之前的保存结果的监听到的数量对保存在数组内的文件描述符进			行for循环遍历，获取到它们的文件描述符，判断该文件描述符为监听文件描述符，则调用接收处理客户端函数，			若接收失败，即最基础的连接都连不上，直接放弃对该文件描述符的其他事件检测，则立即进行下一次for循环；			否则如果该就绪事件的类型为对方关闭连接、挂起、错误其中一个，则取出客户连接资源的定时器，与文件描述符			共同作为参数，调用删除定时器结点函数进行资源的释放；否则如果套接字为管道读端且检测到它的事件为读事			件，即时处理定时器信号，则调用处理信号函数，进行处理信号，ALARM接收成功则timeout定时处理函数功能开			启执行，或SIGTERM接收成功则stop_server服务器关闭，在判断如果处理信号失败，则调用日志错误系统打印日			志；否则如果该文件描述符的事件为读事件，则调用本类的处理客户连接上接收到的数据函数；否则如果该文件描			述符的事件为写事件，则调用本类的向客户连接写入数据函数。判断刚刚设置的timeout定时处理函数功能是否开			启，若是则通过工具类对象调用定时器处理函数，完成定时任务处理函数，同时重新设置警告信号的发出倒计时，			然后调用日志通知系统打印定时器已被处理的日志，同时设置timeout定时处理函数功能为关闭。最后判断。完全			执行完后再通过刚刚设置的stop_server服务器是否已为关闭，而停止服务器，进而停止对epoll树循环读取

> EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用	可能返回一个EINTR错误。如：在socket服务器端，设置了信号捕获机制，有子进程，当在父进程阻塞于慢系	统调用时由父进程捕获到了一个有效信号时，内核会致使accept返回一个EINTR错误(被中断的系统调用)。在	epoll_wait时，因为设置了alarm定时触发警告，导致每次返回-1，errno为EINTR，对于这种错误返回忽略这种	错误，对于这种错误返回进行忽略
>
> > 慢系统调用：该术语适用于那些可能永远阻塞的系统调用。永远阻塞的系统调用是指调用永远无法返	 	回，多数网络支持函数都属于这一类。如：若没有客户连接到服务器上，那么服务器的accept调用就	会永远阻塞
> >
> > > 慢系统调用主要包括以下几个类别：
> > >
> > > 读写‘慢’设备（包括pipe，终端设备，网络连接等）。读时，数据不存在，需要等待；写时，缓	冲区满或其他原因，需要等待
> > >
> > > 当打开某些特殊文件时，需要等待某些条件，才能打开。例如：打开中断设备时，需要等到连	接设备的modem响应才能完成
> > >
> > > pause和wait函数。pause函数使调用进程睡眠，直到捕获到一个信号。wait等待子进程终止

### 设计总结：

初始化各类资源，包括线程池、数据库池、日志系统等资源

然后事件监听，包括网络套接字连接，Socket网络编程基础步骤实现，以及信号量的监听函数功能实现。先是网络套接字	的前半监听的部分的实现，即先用 socket 创建套接字，为监听套接字 bind 绑定需要被监听的地址和端口号，使用 	 	listen 监听这个文件描述符是否有连接到来；epoll_creat注册内核事件表，将监听套接字与管道套接字读端统一交由	 	epoll处理构成统一事件源；以及信号的监听捕捉的设置，还需要设置监听后重设新新闹钟信号

实现定时器的创建、调整和删除功能

接下来就是接收处理客户端功能实现。后半套接字后半处理的实现，即 accept 对 listen监听到的连接请求进行接收，利用	这些初始化好的资源信息挂载到定时器结点上形成一个定时器结点，等待后续的超时处理

然后就是接收和发送因在epoll树上被监听的文件描述符的数据，可分别使用reactor和proactor这两种不同的I/O实现

最后就是主线程的实现，使用 epoll_wait 监听树上的各类事件进行处理，包括：接收处理客户端事件、处理异常事件、处	理定时器信号事件、处理客户连接上接收到的数据、处理向客户连接写入数据，每种请求都需要主线程自己或按情况交	给其他函数去执行

**五种IO模型：**阻塞I/O，非阻塞I/O，信号驱动I/O和I/O复用都是同步I/O。同步I/O指内核向应用程序通知的是就绪事件，比如只通知有客户端连接，要求用户代码自行执行I/O操作，异步I/O是指内核向应用程序通知的是完成事件，比如读取客户端的数据后才通知应用程序，由内核完成I/O操作

> - **阻塞IO**:调用者调用了某个函数，等待这个函数返回，期间什么也不做，不停的去检查这个函数有没有返回，必须等这个函数返回才能进行下一步动作
> - **非阻塞IO**:非阻塞等待，每隔一段时间就去检测IO事件是否就绪。没有就绪就可以做其他事。非阻塞I/O执行系统调用总是立即返回，不管时间是否已经发生，若时间没有发生，则返回-1，此时可以根据errno区分这两种情况，对于accept，recv和send，事件未发生时，errno通常被设置成eagain
> - **信号驱动IO**:linux用套接口进行信号驱动IO，安装一个信号处理函数，进程继续运行并不阻塞，当IO时间就绪，进程收到SIGIO信号。然后处理IO事件
> - **IO复用**:linux用select/poll函数实现IO复用模型，这两个函数也会使进程阻塞，但是和阻塞IO所不同的是这两个函数可以同时阻塞多个IO操作。而且可以同时对多个读操作、写操作的IO函数进行检测。知道有数据可读或可写时，才真正调用IO操作函数
> - **异步IO**:linux中，可以调用aio_read函数告诉内核描述字缓冲区指针和缓冲区的大小、文件偏移及通知的方式，然后立即返回，当内核将数据拷贝到缓冲区后，再通知应用程序

**事件处理模式**

- reactor模式中，主线程(I/O处理单元)只负责监听文件描述符上是否有事件发生，有的话立即通知工作线程(逻辑单元   )，读写数据、接受新连接及处理客户请求均在工作线程中完成。通常由同步I/O实现
- proactor模式中，主线程和内核负责处理读写数据、接受新连接等I/O操作，工作线程仅负责业务逻辑，如处理客户请  求。通常由异步I/O实现

**同步I/O模拟proactor模式：**由于异步I/O并不成熟，实际中使用较少，本此使用同步I/O模拟实现proactor模式

> 同步I/O模型的工作流程如下（epoll_wait为例）：
>
> - 主线程往epoll内核事件表注册socket上的读就绪事件
> - 主线程调用epoll_wait等待socket上有数据可读
> - 当socket上有数据可读，***epoll_wait通知主线程,主线程从socket循环读取数据***，直到没有更多数据可读，然后将读取到的数据封装成一个请求对象并插入请求队列
> - 睡眠在请求队列上某个工作线程被唤醒，它获得请求对象并处理客户请求，然后往epoll内核事件表中注册该socket上的写就绪事件
> - 主线程调用epoll_wait等待socket可写
> - 当socket上有数据可写，epoll_wait通知主线程。主线程往socket上写入服务器处理客户请求的结果

**并发编程模式：** 并发编程方法的实现有多线程和多进程两种，但这里涉及的并发模式指I/O处理单元与逻辑单元的协同完	成任务的方法

> - 半同步/半异步模式 ✓
> - 领导者/追随者模式

**半同步/半反应堆：**半同步/半反应堆并发模式是半同步/半异步的变体，将半异步具体化为某种事件处理模式

> 并发模式中的同步和异步：
>
> - 同步指的是程序完全按照代码序列的顺序执行
> - 异步指的是程序的执行需要由系统事件驱动

> 半同步/半异步模式工作流程：
>
> - 同步线程用于处理客户逻辑
> - 异步线程用于处理I/O事件
> - 异步线程监听到客户请求后，就将其封装成请求对象并插入请求队列中
> - 请求队列将通知某个工作在***同步模式的工作线程***来读取并处理该请求对象

> 半同步/半反应堆工作流程（以Proactor模式为例）:
>
> - 主线程充当异步线程，负责监听所有socket上的事件
> - 若有新请求到来，主线程接收之以得到新的连接socket，然后往epoll内核事件表中注册该socket上的读写事件
> - 如果连接socket上有读写事件发生，主线程从socket上接收数据，并将数据封装成请求对象插入到请求队列中
> - 所有工作线程睡眠在请求队列上，当有任务到来时，通过竞争（如互斥锁）获得任务的接管权

**统一事件源：**统一事件源，是指将信号事件与其他事件一样被处理。具体的，信号处理函数使用管道将信号传递给主循	 	环，信号处理函数往管道的写端写入信号值，主循环则从管道的读端读出信号值，使用I/O复用系统调用来监听管道读	端的可读事件，这样信号事件与其他文件描述符都可以通过epoll来监测，从而实现统一处理