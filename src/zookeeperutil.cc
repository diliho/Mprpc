#include"zookeeperutil.h"
#include"mprpcapplication.h"
#include<iostream>

void global_watcher(zhandle_t*zh,int type,int state,const char*path,void *watcherCtx)
{
    if(type==ZOO_SESSION_EVENT)
    {
        if(state==ZOO_CONNECTED_STATE)
        {
            sem_t *sem=(sem_t*)zoo_get_context(zh);
            sem_post(sem);
        }
    }
}

ZKClient::ZKClient():m_zhandle(nullptr){}
ZKClient::~ZKClient()
{
    if(m_zhandle!=nullptr)
    {
        zookeeper_close(m_zhandle);
    }
}

void ZKClient::Start()
{
    std::string host=MprpcApplication::GetInstance().GetConfig().Load("zookeeperip");
    std::string port=MprpcApplication::GetInstance().GetConfig().Load("zookeeperport");
    std::string conststr=host+":"+port;

    // 创建zookeeper句柄 异步
    m_zhandle=zookeeper_init(conststr.c_str(),global_watcher,3000,nullptr,nullptr,0);
    if(nullptr==m_zhandle){
        std::cout<<"zookeeper_init error"<<std::endl;
        exit(EXIT_FAILURE);
    }
    sem_t sem;
    sem_init(&sem,0,0);
    zoo_set_context(m_zhandle,&sem);
    sem_wait(&sem);
    std::cout<<"zookeeper_init success"<<std::endl;

}
void ZKClient::Create(const char* path,const char* data,int datalen,int state)
{
    char path_buffer[128];
    int bufferlen=sizeof(path_buffer);
    int flag;
    // 先判断path表示的znode节点是否存在，如果存在，就不再重复创建了
    flag= zoo_exists(m_zhandle,path,0,nullptr);
    if(ZNONODE==flag)
    {    
        flag=zoo_create(m_zhandle,path,data,datalen,&ZOO_OPEN_ACL_UNSAFE,state,path_buffer,bufferlen);
        if(flag==ZOK)
        {std::cout<<"znode create success... path:"<<path<<std::endl;}
        else
        {
            std::cout<<"flag:"<<flag<<std::endl;
            std::cout<<"znode create error... path:"<<path<<std::endl;
            exit(EXIT_FAILURE);
        }
    }
   
}
// 根据参数指定的znode节点路径，获取znode节点的值
std::string ZKClient::GetData(const char* path)
{
    char buffer[64];
    int bufferlen=sizeof(buffer);
    int flag=zoo_get(m_zhandle,path,0,buffer,&bufferlen,nullptr);
    if(flag!=ZOK)
    {
        std::cout<<"get znode error... path:"<<path<<std::endl;
        return "";
    }
    else
    {
        std::cout<<"get znode success... path:"<<path<<" data:"<<buffer<<std::endl;
        return buffer;
    }
}
