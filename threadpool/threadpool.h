#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
using namespace std;

template <typename T>
class threadpool
{
public:
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    //使用静态修饰成员函数，消除this指针作为参数(this)对函数参数要求类型(void *)的影响，
    //我们需要的是(void this)，即将this放在第四个参数
    //在线程创建的函数种就可以直接使用 类名：函数名 去调用函数，在类的作用域下还能省略类名
    static void *worker(void *arg);
    void run();

private:
    //线程池中的线程数
    int m_thread_number;
    //请求队列中允许的最大请求数
    int m_max_requests;
    //描述线程池的数组，大小为m_thread_number
    pthread_t *m_threads;
    //请求队列，注意为指针类型
    list<T *> m_workqueue;
    //保护请求队列的互斥锁
    locker m_queuelocker;
    //是否有任务需要处理
    sem m_queuestat;
    //数据库连接池
    connection_pool *m_connPool;
    //Reactor/Proactor模型切换
    int m_actor_model;
};

template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool) 
{
    if(thread_number <= 0 || max_requests <= 0)
    {
        throw exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads)
    {
        throw exception();
    }
    for(int i = 0; i < thread_number; ++i)
    {
        // 将线程的入口点函数定义为静态函数，以满足pthread_create函数的要求，
        // 并通过传递类实例的this指针作为参数，实现类成员的访问
        if(pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete [] m_threads;
            throw exception();
        }
        // 线程分离，将线程属性更改为unjoinable，系统自动释放线程资源
        if(pthread_detach(m_threads[i])) 
        {
            delete [] m_threads;
            throw exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool()
{
    delete [] m_threads;
}

//reactor模式下向请求队列中添加任务
template<typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    //标记读写事件 (proactor中没有此端代码，读和写都已经在添加任务函数前执行)
    request->m_state = state; 
    //添加任务
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    //通知有一个新的任务可以被处理
    m_queuestat.post();  
    return true;
}

//proactor模式下向请求队列中添加任务
template<typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests) 
    {
        m_queuelocker.unlock(); 
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();  
    return true;
}

//工作线程处理函数
template<typename T>
void *threadpool<T>::worker(void *arg)
{
    // 通过 arg 参数可以获得当前 threadpool 对象的指针即创建线程时的第四个参数this，
    // 并调用相应的成员函数来处理工作队列中的任务。
    // 这样新线程就可以访问当前 threadpool 对象。将参数强转为线程池类，调用成员方法。
    threadpool *pool = (threadpool *)arg; 
    //工作线程在创建后都会进入此函数
    pool->run();
    return pool;
}

//在本次项目中T类型即为http_conn类型
template<typename T>
void threadpool<T>::run()
{
    while (true)
    {
        //信号量等待，线程池中的所有线程都阻塞，等待请求队列中新增任务
        m_queuestat.wait();
        //被唤醒后先加互斥锁保证线程安全
        m_queuelocker.lock();  
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        //从请求队列中取出第一个任务
        T *request = m_workqueue.front();
        //将该任务从请求队列删除
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request)
        {
            continue;
        }
        //reactor模式。工作线程进行数据的读写等操作
        if(m_actor_model == 1)
        {
            //IO事件类型：0为读
            if(request->m_state == 0)
            {
                //调用循环读取客户数据，直到无数据可读或对方关闭连接
                if(request->read_once()) 
                {
                    //是否正在处理数据中
                    request->improv = 1; 
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    //处理http报文请求与报文响应
                    request->process(); 
                }
                else
                {
                    request->improv = 1;
                    //关闭连接
                    request->timer_flag = 1; 
                }
            }
            else
            {
                //往响应报文写入数据
                if(request->write()) 
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        //proactor模式。主线程和内核已负责处理读写数据、接受新连接等I/O操作，此工作线程仅负责业务逻辑，处理客户请求等
        else
        {
            connectionRAII mysql(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif 