#include "log.h"
using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false;
}

Log::~Log()
{
    if(m_fp != NULL)
    {
        fclose(m_fp);
    }
}

bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    //若设置了max_queue_size,则设置为异步写日志，否则设0视同步
    if(max_queue_size >= 1)
    {
        m_is_async = true;
        //创建并设置阻塞队列长度
        m_log_queue = new block_queue<string>(max_queue_size);

        pthread_t tid;
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    //日志的最大行数
    m_spilt_lines = split_lines;

    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    //获取本地时区的时间
    struct tm my_tm = *sys_tm;  

    //查找字符最后一次出现的位置，并返回指向该位置的指针。
    const char *p = strrchr(file_name, '/'); 
    char log_full_name[256] = {0};

    if(p == NULL)
    {
        //若输入的文件名没有/，则直接将时间+文件名作为日志名
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        strcpy(log_name, p + 1);
        //file_name为数组首地址，指向文件名最后一个/符的p - file_name + 1即为路径名
        strncpy(dir_name, file_name, p - file_name + 1); 
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    // 追加写入内容
    m_fp = fopen(log_full_name, "a"); 
    if(m_fp == NULL)
    {
        return false;
    }
    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t); // 传入参数，作修改结构体内的参数
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    switch(level)
    {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }

    m_mutex.lock();
    //写入一个log，对m_count++
    m_count++;

    //判断当前day是否为创建日志的时间，行数是否超过最大行限制
    if(m_today != my_tm.tm_mday || m_count % m_spilt_lines == 0)
    {
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};
        
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if(m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
            //若行数超过最大行限制，在当前日志的末尾加count/max_lines为后缀创建新log
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_spilt_lines);
        }
        m_fp = fopen(new_log, "a");
    }
    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();
    int n= snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                    my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, 
                    my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    //内容格式化，用于向字符串中打印数据、数据格式用户自定义，返回写入到字符数组str中的字符个数(不包含终止符)
    // +n 表示将字符数组 m_buf 的起始地址偏移 n 个字符,将二者组装到一起
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst); 
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;
    m_mutex.unlock();

    if(m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        m_mutex.lock();
        //将字符串写入文件中
        fputs(log_str.c_str(), m_fp); 
        m_mutex.unlock();
    }
    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}