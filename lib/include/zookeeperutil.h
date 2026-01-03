#pragma once
#include <semaphore.h>//信号量
#include <zookeeper/zookeeper.h>
#include<string>

class ZKClient
{
public:
    ZKClient();
    ~ZKClient();
    // 启动zookeeper客户端连接
    void Start();
    // 在zookeeper上根据指定的path创建znode节点
    void Create(const char* path, const char* data, int datalen, int state=0);
    // 根据参数指定的znode节点路径，获取znode节点的值
    std::string GetData(const char* path);
private:
    // zk的客户端句柄
    zhandle_t* m_zhandle;
    // 添加信号量成员变量
    sem_t m_sem;
    
    // 递归创建父节点（私有方法）
    void CreateParentNodes(const char* path);
};