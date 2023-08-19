#ifndef LOG_H
#define LOG_H
#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include "block_queue.h"
using namespace std;

class Log
{
public:
    //局部静态变量的线程安全懒汉模式
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    //回调函数，异步写日志公有方法，调用私有方法async_write_log，
    //它的操作与类内属性有关，因此内类声明和定义，并且较简单直接声明定义一起写
    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }

    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 50000000, int max_queue_size = 0);

    //将输出内容按照标准格式整理
    void write_log(int level, const char *format, ...);

    //强制刷新缓冲区
    void flush(void);

private:
    Log();
    virtual ~Log();

    //异步写日志方法
    void *async_write_log()
    {
        string single_log;

        //从阻塞队列中取出一条日志内容，写入文件
        while(m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp); 
            m_mutex.unlock();
        }
    }

private:
    //路径名
    char dir_name[128];
    //log文件名
    char log_name[128];
    //日志最大行数
    int m_spilt_lines;
    //日志缓冲区大小
    int m_log_buf_size;
    //日志行数记录
    long long m_count;
    //按天分文件,记录当前时间是那一天
    int m_today;
    //打开log的文件指针
    FILE *m_fp;
    //要输出的内容
    char *m_buf;
    //阻塞队列
    block_queue<string> *m_log_queue;
    //是否同步标志位
    bool m_is_async;
    //同步类
    locker m_mutex;
    //关闭日志，0开启
    int m_close_log;
};

#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif 