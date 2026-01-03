#include "mprpcprovider.h"
#include "mprpccontroller.h"
#include "rpcerror.h" // 添加错误码头文件
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

// 启动rpc服务节点，开始提供rpc远程网络调用服务
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
    // 注册服务节点到zookeeper上
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


// 辅助结构体，用于存储回调参数
struct RpcResponseArgs {
    RpcProvider* provider;
    muduo::net::TcpConnectionPtr conn;
    google::protobuf::Message* response;
    MprpcController* controller;
};

// 回调函数 - 现在使用void*参数并进行类型转换
void OnRpcResponse(void* args) {
    // 将void*转换为RpcResponseArgs*
    RpcResponseArgs* responseArgs = static_cast<RpcResponseArgs*>(args);
    
    // 调用SendRpcResponse方法
    responseArgs->provider->SendRpcResponse(
        responseArgs->conn,
        responseArgs->response,
        responseArgs->controller
    );
    
    // 释放参数结构体
    delete responseArgs;
}

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
    
        // 创建错误码和错误对象
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::DESERIALIZE_FAILED, 0, "MPRPC");
        RpcError error(error_code, "rpc_header_str parse error!");
        // 可以考虑发送错误响应给客户端
        return;
    }
    
    // 获取rpc方法参数的字符流
    std::string args_str = recv_buf.substr(4 + header_size, args_size);

    //获取rpc服务和方法(字符串)
    auto it = m_serviceMap.find(service_name);
    if (it == m_serviceMap.end())
    {
        // 使用新的错误码体系：服务不存在
        LOG_ERROR("service_name:%s not find", service_name.c_str());
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::SERVICE_DISCOVERY_FAILED, 0, "MPRPC");
        RpcError error(error_code, "service_name:" + service_name + " not found");
        // 可以考虑发送错误响应给客户端
        return;
    }
    
    auto mit = it->second.m_methodMap.find(method_name);
    if (mit == it->second.m_methodMap.end())
    {
        // 使用新的错误码体系：方法不存在
        LOG_ERROR("method_name:%s not find", method_name.c_str());
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::SERVICE_DISCOVERY_FAILED, 0, "MPRPC");
        RpcError error(error_code, "method_name:" + method_name + " not found");
        // 可以考虑发送错误响应给客户端
        return;
    }
    
    //获取service对象和方法
    google::protobuf::Service* service = it->second.m_service;
    const google::protobuf::MethodDescriptor* method = mit->second;
    
    // 创建控制器对象，用于传递错误信息
    MprpcController* controller = new MprpcController();
    
    //生成rpc方法调用的response和 request
    google::protobuf::Message* request = service->GetRequestPrototype(method).New();
    if (!request->ParseFromString(args_str))
    {
        // 使用新的错误码体系：请求参数解析失败
        LOG_ERROR("request parse error!");
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::DESERIALIZE_FAILED, 0, "MPRPC");
        RpcError error(error_code, "request parse error!");
        controller->SetFailed(error);
        // 可以考虑发送错误响应给客户端
        delete controller;
        delete request;
        return;
    }
    
    google::protobuf::Message* response = service->GetResponsePrototype(method).New();

    // 创建辅助结构体并设置参数
    RpcResponseArgs* args = new RpcResponseArgs();
    args->provider = this;
    args->conn = conn;
    args->response = response;
    args->controller = controller;

    // 创建回调 - 使用static_cast将args转换为void*
    google::protobuf::Closure* done = google::protobuf::NewPermanentCallback(
        OnRpcResponse, 
        static_cast<void*>(args)
    );

    //调用当前节点发布的方法
    service->CallMethod(method, controller, request, response, done);
    
    // 释放request，response和controller将在SendRpcResponse中释放
    delete request;
}

// 修改SendRpcResponse方法，添加controller参数
void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr& conn, 
                                 google::protobuf::Message* response, 
                                 MprpcController* controller)
{
    std::string response_str;
    std::string send_rpc_str;
    
    // 如果有错误，我们应该在响应中包含错误信息
    if (controller->Failed()) {
        LOG_ERROR("RPC call failed: %s", controller->ErrorText().c_str());
    } else {
        // response序列化
        if (response->SerializeToString(&response_str))
        {
            // 构造RPC响应头
            mprpc::RpcHeader rpcHeader;
            // 注意：这里的service_name和method_name可以为空，或者根据实际情况设置
            rpcHeader.set_service_name("");
            rpcHeader.set_method_name("");
            rpcHeader.set_args_size(response_str.size());
            
            std::string rpc_header_str;
            if (rpcHeader.SerializeToString(&rpc_header_str)) {
                // 组织发送的数据：header_size + header_str + response_str
                uint32_t header_size = rpc_header_str.size();
                send_rpc_str.insert(0, std::string((char*)&header_size, 4));
                send_rpc_str += rpc_header_str;
                send_rpc_str += response_str;
                
                //通过muduo返回rpc方法的结果
                conn->send(send_rpc_str);
            } else {
                LOG_ERROR("serialize rpc header error!");
            }
        }
        else
        {
            // 使用新的错误码体系：响应序列化失败
            LOG_ERROR("serialize response error!");
            auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::SERIALIZE_FAILED, 0, "MPRPC");
            RpcError error(error_code, "serialize response error!");
        }
    }
    
    // 释放资源
    conn->shutdown();
    delete response;
    delete controller;
}



