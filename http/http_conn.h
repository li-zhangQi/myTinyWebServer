#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <map>
using namespace std;

#include "../lock/locker.h"

class http_conn
{
    public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    //报文的请求方法
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    //报文解析的结果
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOUCE,
        FORBIDDEN_REQUEST,
        FIFE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    //从状态机的状态，文本解析是否成功
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    public:
    http_conn(){};
    ~http_conn(){};

    public:
    void init(int sockfd, const sockaddr_in &addr, char *,int, int, string user,string passwd, string sqlname);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    //
    int timer_flag;
    int improv;

    private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line()
    {
        return m_read_buf + m_start_line;
    };
    LINE_STATUS parse_line();
    void unmap(); // ?

    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

    public:
    static int m_epollfd;
    static int m_user_count;
    // 
    int m_state;

    private:
    int m_sockfd;
    sockaddr_in m_address;
	//存储读取的请求报文数据
    char m_read_buf[READ_BUFFER_SIZE];
	//缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    long m_read_idx;
	//m_read_buf读取的位置
    long m_checked_idx;
	//m_read_buf中已经解析的字符个数
    long m_start_line;
	//存储发出的响应报文数据
    char m_write_buf[WRITE_BUFFER_SIZE];
	//指示buffer中的长度
    int m_write_idx;
	//主状态机的状态
    CHECK_STATE m_check_state;
	//请求方法
    METHOD m_method;

	//以下为解析请求报文中对应的6个变量
    //存储读取文件的名称
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    //用来存储请求头部表示的请求主体数据的内容长度字段，GET请求为长度为0,POST有长度
    int m_content_length;
    //判断POST请求是否为长连接
    bool m_linger;

	//读取服务器上的文件地址
    char *m_file_address;
    struct stat m_file_stat;
	//io向量机制iovec
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
	//剩余发送字节数
    int bytes_to_send;
	//已发送字节数
    int bytes_have_send;
    char *doc_root;

    map<string, string> m_users;//用户名密码匹配表
    int m_TRIGMode;//触发模式
    int m_close_log;//是否开启日志

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};
#endif