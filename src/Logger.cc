#include"logger.h"
#include<time.h>
#include<iostream> 

Logger& Logger::GetInstance()
{  
    static Logger logger;
    return logger;

   
}

//负责从队列写到日志文件
Logger::Logger()
{
    //启动写日志线程
    std::thread writeLogTask([&]() {
        for(;;)
        {
            //获取当前的日期，然后取日志信息，写入相应的日志文件中
             time_t now=time(nullptr);
             tm *nowtm=localtime(&now);

             char file_name[128];
             sprintf(file_name,"%d-%d-%d-log.txt",nowtm->tm_year+1900,nowtm->tm_mon+1,nowtm->tm_mday);

             FILE* pf=fopen(file_name,"a+");
             if(pf==nullptr)
             {
                std::cout<<"logger file: "<<file_name<<" open error!"<<std::endl;
                exit(EXIT_FAILURE);
             }

             std::string msg=m_lockqueue.Pop();
             fputs(msg.c_str(),pf);
             fputs("\n",pf);
             fclose(pf);
        }
    }); 
    //设置分离线程，守护线程
    writeLogTask.detach();


 }
void Logger::SetLogLevel(LogLevel level)
{
    m_loglevel=level;

}
//写日志，先把日志信息写入lockqueue缓冲区中
void Logger::Log(std::string msg)
{
  //设置日志级别
  //添加事件发生时间
   time_t now=time(nullptr);
    tm *nowtm=localtime(&now);

    char time_buf[128]={0};
    const char*loglevel_str=m_loglevel==INFO?"INFO":"ERROR";
    
    sprintf(time_buf,"%d-%02d-%02d %02d:%02d:%02d [%s] ",
            nowtm->tm_year+1900, nowtm->tm_mon+1, nowtm->tm_mday,
            nowtm->tm_hour, nowtm->tm_min, nowtm->tm_sec, loglevel_str);
     msg.insert(0,time_buf);
     
  m_lockqueue.Push(msg);
}
