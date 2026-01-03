#include "mprpcprovider.h"
#include <string>
#include "mprpcapplication.h"
#include <functional>
#include <google/protobuf/descriptor.h>
#include "rpcheader.pb.h"


// 将函数实现为RpcProvider类的成员函数
void RpcProvider::NotifyService(google::protobuf::Service *service)
{
    ServiceInfo service_info;

    // 获取服务对象的描述信息
    const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();
    // 获取服务对象的名称
    std::string service_name = pserviceDesc->name();
    // 获取服务对象的方法数量
    int methodCnt = pserviceDesc->method_count();

 
    LOG_INFO("service_name:%s", service_name.c_str());

    // 遍历服务对象的方法
    for (int i = 0; i < methodCnt; ++i)
    {
        // 获取服务对象的第i个方法的描述信息
        const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
        std::string method_name = pmethodDesc->name();
       
        LOG_INFO("method_name:%s", method_name.c_str());
        // 存储方法名称和方法描述信息的映射
        service_info.m_methodMap.insert({method_name, pmethodDesc});
    }
    service_info.m_service = service;
    m_serviceMap.insert({service_name, service_info});
}

// 将函数实现为RpcProvider类的成员函数
void RpcProvider::Run()
{
    std::string str_ip = "rpcserverip";
    std::string ip = MprpcApplication::GetConfig().Load(str_ip);
    std::string str_port = "rpcserverport";
    uint16_t port = atoi(MprpcApplication::GetConfig().Load(str_port).c_str());
    muduo::net::InetAddress address(ip, port);

    // 创建TcpServer对象
    muduo::net::TcpServer server(&m_eventloop, address, "RpcProvider");
    // 绑定连接回调和消息读写回调
    server.setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    server.setMessageCallback(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    // 设置muduo库的线程数量
    server.setThreadNum(4);
    
    // 使用成员变量的ZKClient
    m_zkclient.Start();
    for(auto &sp:m_serviceMap)
    {
        std::string service_path="/"+sp.first;
        m_zkclient.Create(service_path.c_str(),nullptr,0);
        for(auto &mp:sp.second.m_methodMap)
        {
            std::string method_path=service_path+"/"+mp.first;
            char method_path_data[128]={0};
            sprintf(method_path_data,"%s:%d",ip.c_str(),port);
            m_zkclient.Create(method_path.c_str(),method_path_data,strlen(method_path_data),ZOO_EPHEMERAL);
        }
    }
    
    LOG_INFO("RpcProvider start service at ip:%s port:%d", ip.c_str(), port);

    // 启动服务器
    server.start();
    m_eventloop.loop();
}
void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn)
{
    if (!conn->connected())
    {
        // 断开连接
        conn->shutdown();
    }
}

/*
    定义通信协议
    header_size(4B) + header_str + args
    eg:16UseserviceRpcLogin1111222233334444
    其中header_size表示消息头部的长度，
    header_str表示消息头部的内容，
    args表示消息体的内容
*/
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn,
                            muduo::net::Buffer *buffer,
                            muduo::Timestamp)
{
    // 读取网络数据，buffer中存储的是rpc调用的请求数据
    std::string recv_buf = buffer->retrieveAllAsString();

    // 从recv_buf中读取前4个字节的header_size
    uint32_t header_size = 0;
    recv_buf.copy((char *)&header_size, 4, 0);

    // 根据header_size读取数据头的原始字符流
    std::string rpc_header_str = recv_buf.substr(4, header_size);
    // 反序列化rpc_header_str，得到rpc请求的详细信息
    mprpc::RpcHeader rpcHeader;
    std::string service_name;
    std::string method_name;
    uint32_t args_size = 0;
    if (rpcHeader.ParseFromString(rpc_header_str))
    {
        service_name = rpcHeader.service_name();
        method_name = rpcHeader.method_name();
        args_size = rpcHeader.args_size();
    }
    else
    {
        

        LOG_INFO("rpc_header_str parse error!");
        return;
    }
    // 获取rpc方法参数的字符流
    std::string args_str = recv_buf.substr(4 + header_size, args_size);

    //获取rpc服务和方法(字符串)
    auto it=m_serviceMap.find(service_name);
    if(it==m_serviceMap.end())
    {
       
        LOG_INFO("service_name:%s not find",service_name.c_str());
        return;
    }
    auto mit=it->second.m_methodMap.find(method_name);
    if(mit==it->second.m_methodMap.end())
    {
        
        LOG_INFO("method_name:%s not find",method_name.c_str());
        return ;
    }
    //获取service对象和方法
    google::protobuf::Service*service=it->second.m_service;
    const google::protobuf::MethodDescriptor*method=mit->second;
    
    /*生成rpc方法调用的response和 request
     void Login(::google::protobuf::RpcController *controller,
               const ::fixbug::LoginRequest *request,
               ::fixbug::LoginResponse *response,
               ::google::protobuf::Closure *done) override
    */
    google::protobuf::Message*request=service->GetRequestPrototype(method).New();
    if(!request->ParseFromString(args_str))
    {
        LOG_ERROR("request parse error!"); 
        return ;
    }
    google::protobuf::Message*response=service->GetResponsePrototype(method).New();

    //绑定一个Closure的回调函数
    google::protobuf::Closure*done=
    google::protobuf::NewCallback<RpcProvider,const::muduo::net::TcpConnectionPtr&,google::protobuf::Message*>
                                (this,&RpcProvider::SendRpcResponse,conn,response);


    
    //调用当前节点发布的方法
    service->CallMethod(method,nullptr,request,response,done);
}

    //Closure的回调 序列化rpc响应
    void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr&conn,google::protobuf::Message*response)
    {
        std::string response_str;
        //response序列化
        if(response->SerializeToString(&response_str))
        {
            //通过muduo返回rpc方法的结果
            conn->send(response_str);
        }
        else{
           
            LOG_ERROR("serialize  response error!"); 
        }
        conn->shutdown();
        
    }



