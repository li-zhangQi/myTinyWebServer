#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_
#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include <stdlib.h>
#include <pthread.h>

#include "../lock/locker.h"
#include "../log/log.h"
using namespace std;

class connection_pool
{
public:
    //获取数据库连接
    MYSQL *GetConnection();
    //释放连接
    bool ReleaseConnection(MYSQL *conn);
    //获取连接
    int GetFreeConn();
    //销毁所有连接
    void DestroyPool();

    //单例模式
    static connection_pool *GetInstance();

    //初始化连接池
    void init(string url, string User, string PassWord, string DatabaseName, int Port, int MaxConn, int close_log);

private:
    connection_pool();
    ~connection_pool();

    //最大连接数
    int m_MaxConn;
    //当前已使用的连接数
    int m_CurConn;
    //当前空闲的连接数
    int m_FreeConn;
    //连接池
    list<MYSQL *> connList;

    locker lock;
    sem reserve;

public:
    //主机地址
    string m_url;
    //数据库端口号
    string m_Port;
    //登陆数据库用户名
    string m_User;
    //登陆数据库密码
    string m_PassWord;
    //使用数据库名
    string m_DatabaseName;
    //日志开关
    int m_close_log; 
};

class connectionRAII
{
public:
    //双指针对MYSQL *con修改，实现在获取连接时对传入的参数进行修改
    connectionRAII(MYSQL **con, connection_pool *connPool);
    ~connectionRAII();

private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};

#endif