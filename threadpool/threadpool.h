#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
// 
using namespace std;

template <typename T>
class threadpool
{
    // 模板类成员函数，类内声明，类外定义
    public:
    threadpool(int actor_model, /*,*/ int thread_number = 8, int max_requests = 1000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

    private:
    static void *worker(void *arg);
    void run();

    private:
    int m_thread_number;
    int m_max_requests;
    pthread_t *m_threads;

    list<T *> m_workqueue;
    locker m_queuelocker;
    sem m_queuestat;
    // 
    int m_actor_model;
};

template <typename T>
threadpool<T>::threadpool(int actor_model, /*,*/ int thread_number, int max_requests) : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL) /*,*/  // 列表初始化 m_actor_model(actor_model): 这表示对 m_actor_model 成员变量进行初始化，并将传递进来的 actor_model 参数的值赋给 m_actor_model。
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
        // 在多线程编程中，通常将线程的入口点函数定义为静态函数，以满足pthread_create函数的要求，并通过传递类实例的this指针作为参数，实现类成员的访问。
        if(pthread_create(m_threads + 1, NULL, worker, this) != 0) // 返回值非0为出错
        {
            delete [] m_threads;
            throw exception();
        }
        // 线程分离
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
template<typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests) // 工作队列已满，不能再添加更多的请求。在这种情况下，该函数会立即返回 false，表示添加请求失败。
    {
        m_queuelocker.unlock(); // 出现错误先解锁再终止？
        return false;
    }
    request->m_state = state; // 此IO事件类别默认为读0
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuelocker.post();  // 通知已有一个工作队列的空位
    return true;
}
template<typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg; // 通过 arg 参数可以获得当前 threadpool 对象的指针，并调用相应的成员函数来处理工作队列中的任务。这样新线程就可以访问当前 threadpool 对象
    pool.run();
    return pool;
}
template<typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait(); // 无数据时信号量阻塞
        m_queuelocker.lock();  // 有数据不阻塞了就加锁
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request)
        {
            continue;
        }
        if(m_actor_model == 1)
        {
            //IO事件类型：0为读
            if(request->m_state == 0)
            {
                if(request->read_once()) // 循环读取客户数据，直到无数据可读或对方关闭连接
                {
                    request->improv = 1; // 是否正在处理数据中
                    // 
                    request->process(); // 处理http报文请求与报文响应
                }
                else
                {
                    request->improc = 1;
                    request->timer_flag = 1; // 是否关闭连接
                }
            }
            else
            {
                if(request->write()) // 往响应报文写入数据
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
        // 
    }
    
}

#endif 