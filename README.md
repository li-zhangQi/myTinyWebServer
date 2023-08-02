# 对项目的整体梳理

___

>注：
>
>T：变量类型
>
>C：参数
>
>H：函数

___



### locker .h 头文件涉及的函数概要

互斥量：

T :	pthread_mutex_t

H :	pthread_mutex_init()	pthread_mutex_destroy()	pthread_mutex_lock()	pthread_mutex_unlock()

信号量：

T :	sem_t

H :	sem_init()	sem_destroy()	sem_wait()	sem_post()

>_wait函数将以原子操作方式将信号量减一,信号量为0时,sem_wait阻塞。
>
>_post函数以原子操作方式将信号量加一,信号量大于0时,唤醒调用sem_post的线程。

条件变量：

T :	pthread_cond_t

H :	pthread_cond_init()	pthread_cond_destroy()	pthread_cond_wait()	pthread_cond_timewait()	

​			pthread_cond_signal()	pthread_cond_broadcast()

>条件变量需要配合锁来使用。
>
>_wait等待条件变量时，线程会先释放持有的互斥锁，然后进入阻塞状态，等待其他线程通过条件变量的通知来唤醒它。在收到通知后，线程会重新获得互斥锁，然后继续执行。





