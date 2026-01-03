#include"zookeeperutil.h"
#include"mprpcapplication.h"
#include<iostream>
#include"logger.h"
#include"rpcerror.h"

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

ZKClient::ZKClient():m_zhandle(nullptr)
{
    sem_init(&m_sem, 0, 0);
}

ZKClient::~ZKClient()
{
    if(m_zhandle!=nullptr)
    {
        zookeeper_close(m_zhandle);
    }
    sem_destroy(&m_sem);
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

        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode:: SERVICE_DISCOVERY_FAILED, 0, "MPRPC");
        RpcError error(error_code, "zookeeper_init error");
       

        exit(EXIT_FAILURE);
    }
    
    zoo_set_context(m_zhandle,&m_sem);
    sem_wait(&m_sem);
   LOG_INFO("zookeeper_init success!")
}
// 递归创建父节点
void ZKClient::CreateParentNodes(const char* path) {
    std::string parent_path(path);
    size_t last_slash = parent_path.find_last_of('/');
    
    // 如果路径只有一层，直接返回
    if (last_slash == 0) {
        return;
    }
    
    // 获取父节点路径
    parent_path = parent_path.substr(0, last_slash);
    
    // 检查父节点是否存在
    int flag = zoo_exists(m_zhandle, parent_path.c_str(), 0, nullptr);
    if (flag == ZNONODE) {
        // 递归创建父节点
        CreateParentNodes(parent_path.c_str());
        
        // 创建父节点
        flag = zoo_create(m_zhandle, parent_path.c_str(), nullptr, 0, &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
        if (flag == ZOK) {
        
            LOG_INFO("SUCCESS: Create parent node: %s", parent_path.c_str());
        } else {
            std::cout << "ERROR: zookeeper服务发现失败"<< std::endl;
            auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode:: SERVICE_DISCOVERY_FAILED, 0, "MPRPC");
            RpcError error(error_code, "Create parent node failed");
           
         
        }
    }
}

// 在zookeeper上根据指定的path创建znode节点
void ZKClient::Create(const char* path, const char* data, int datalen, int state) {
    // 递归创建父节点
    CreateParentNodes(path);
    
    char path_buffer[128];
    int bufferlen = sizeof(path_buffer);
    int flag;
    
    // 先判断path表示的znode节点是否存在，如果存在，就不再重复创建了
    flag = zoo_exists(m_zhandle, path, 0, nullptr);
    if (ZNONODE == flag) {
        flag = zoo_create(m_zhandle, path, data, datalen, &ZOO_OPEN_ACL_UNSAFE, state, path_buffer, bufferlen);
        if (flag == ZOK) {
            std::cout << "SUCCESS: Create znode: " << path << " data: " << (data ? data : "null") << std::endl;
            LOG_INFO("SUCCESS: Create znode: %s", path);
        } else {
            std::cout << "ERROR: Create znode failed: " << path << " error code: " << flag << std::endl;
            auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode:: SERVICE_DISCOVERY_FAILED, 0, "MPRPC");
            RpcError error(error_code, "Create znode failed");
            
            exit(EXIT_FAILURE);
        }
    }
}
// 根据参数指定的znode节点路径，获取znode节点的值
//返回的是rpc方法的ip:prot
std::string ZKClient::GetData(const char* path)
{
    // 检查ZooKeeper句柄是否有效
    if (m_zhandle == nullptr) {
        std::cout << "ERROR: Zookeeper handle is null, cannot get data for path: " << path << std::endl;
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode:: SERVICE_DISCOVERY_FAILED, 0, "MPRPC");
        RpcError error(error_code, "Zookeeper handle is null");
        
     
        return "";
    }
    
    char buffer[64];
    int bufferlen = sizeof(buffer);
    int flag = zoo_get(m_zhandle, path, 0, buffer, &bufferlen, nullptr);
    
    if (flag != ZOK) {
        std::cout << "ERROR: Get znode failed, path: " << path << " error code: " << flag << std::endl;
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode:: SERVICE_DISCOVERY_FAILED, 0, "MPRPC");
        RpcError error(error_code, "Get znode failed");
          return "";
         }
    else
    {
        LOG_INFO("SUCCESS: Get znode data, path: %s", path);
        return buffer;
    }
}
