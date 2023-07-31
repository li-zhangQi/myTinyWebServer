#ifndef LOG_H
#define LOG_H
#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"
using namespace std;

class Log
{
    public:
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 50000000, int max_queue_size = 0);

    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }

    void write_log(int level, const char *format, ...);

    void flush(void);

    private:
    Log();
    virtual ~Log();

    void *async_write_log()
    {
        string single_log;

        while(m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp); // .c_str()应为fputs仅接受c风格的字符串
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
    //关闭日志
    int m_close_log;
};

#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif 