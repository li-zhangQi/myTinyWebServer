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

>_wait函数将以原子操作方式将信号量减一，信号量为0时，sem_wait阻塞。
>
>_post函数以原子操作方式将信号量加一，信号量大于0时，唤醒调用sem_post的线程。

条件变量：

T：pthread_cond_t

H：pthread_cond_init()、pthread_cond_destroy()、pthread_cond_wait()、pthread_cond_timewait()、	

​		pthread_cond_signal()、pthread_cond_broadcast()

>条件变量需要配合锁来使用。
>
>_wait等待条件变量时，线程会先释放持有的互斥锁，然后进入阻塞状态，等待其他线程通过条件变量的通知来唤醒它。在收到通知后，线程会重新获得互斥锁，然后继续执行。

### 包含的功能：

1、互斥量、信号量、条件变量的创建

___



### threadpool.h 头文件涉及函数概要

T：pthread_t

H：pthread_creat()、pthread_detach()	

>_creat函数中，通常将线程的入口点函数定义为静态函数，以满足pthread_create函数的要求，并通过传递类实例的this指针作为参数，实现类成员的访问。如static void *worker(void *arg);

Q： list<T *> m_workqueue //请求队列，注意为指针类型。

​		T *request = m_workqueue.front() //从请求队列中取出第一个任务。

### 包含的功能：

1、创建线程，通过一个静态成员函数，实现成为工作线程

2、创建请求队列，分别在reactor模式和proactor模式下 通过信号量的通知机制将请求任务放入任务队列

3、创建工作线程的处理函数，函数先获取到线程池对象，通过此对象获取到业务处理函数

4、在业务处理函数中，若为reactor模式则调用处理读写IO事件，如果为读事件则调用循环读取用户数据，来处理请求报		文并报文响应，如果为写事件则调用函数往响应报文写入数据；若为proactor模式则直接调用处理请求报文并报文响		应 完成用户请求而无需负责数据的读写。



​        

   

