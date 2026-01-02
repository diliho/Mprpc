#pragma once
#include"lockqueue.h"
#include<string> 

enum LogLevel
{
    INFO,
    ERROR,
};

class Logger
{
public:
static Logger& GetInstance();
  void SetLogLevel(LogLevel level);
  void Log(std::string msg);
private:

    LockQueue<std::string>m_lockqueue;
    int m_loglevel;
    //单列模式
    Logger();
    Logger(const Logger&)=delete;
   Logger(Logger&&)=delete; 

}; 


#define LOG_INFO(logmsgformat,...)\
do\
{\
    Logger& logger=Logger::GetInstance();\
    logger.SetLogLevel(INFO);\
    char buf[1024]={0};\
    sprintf(buf,logmsgformat,##__VA_ARGS__); \
    logger.Log(buf); \
}while(0);

#define LOG_ERROR(logmsgformat,...)\
do\
{Logger& logger=Logger::GetInstance();\
    logger.SetLogLevel(ERROR);\
    char buf[1024]={0};\
    sprintf(buf, logmsgformat,##__VA_ARGS__); \
    logger.Log(buf); \
}while(0);
