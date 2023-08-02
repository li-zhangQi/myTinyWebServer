#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"
using namespace std;

connection_pool::connection_pool()
{
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
    static connection_pool connPool; // 使用类的私有静态指针变量指向类的唯一实例，并用一个公有的静态方法获取该实例。
    return &connPool;
}

void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
    m_url = url;
    m_User = User;
    m_Port = Port;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    m_close_log = close_log;

    for(int i = 0; i < MaxConn; i++)
    {
        MYSQL *con = NULL;
        con = mysql_init(con);

        if(con == NULL)
        {
            LOG_ERROR("MYSQL Error");
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

        if(con == NULL) // 再此为空表示前面创建失败
        {
            LOG_ERROR("MYSQL Error");
            exit(1);
        }

        connList.push_back(con);
        ++m_FreeConn;
    }
    
    reserve = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;    
}

MYSQL *connection_pool::GetConnection()
{
    MYSQL *con = NULL;

    if(connList.size() == 0)
    {
        return NULL;
    }

    reserve.wait();
    lock.lock();
    con = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();
    return con;
}

bool connection_pool::ReleaseConnection(MYSQL *con)
{
    if(con == NULL)
    {
        return false;
    }

    lock.lock();
    connList.push_back(con); // 从新返回给连接池
    ++m_FreeConn;
    --m_CurConn;
    lock.unlock();

    reserve.post();
    return true;
}

void connection_pool::DestroyPool()
{
    lock.lock();
    if(connList.size() > 0)
    {
        list<MYSQL *>::iterator it;
        for(it = connList.begin(); it != connList.end(); ++it)
        {
            MYSQL *con = *it;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}

int connection_pool::GetFreeConn()
{
    return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) // 通过在对象的构造函数中获取资源, 无论是程序正常结束还是在任意位置抛出异常，都能确保文件资源得到正确释放。
{
    *SQL = connPool->GetConnection();
    conRAII =  *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(conRAII);
}