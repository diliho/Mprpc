#pragma once
#include "google/protobuf/service.h"
#include <memory>
#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <unordered_map>
#include "zookeeperutil.h" 

class RpcProvider
{
public:
    // 发布rpc方法
    void NotifyService(google::protobuf::Service *service);
    // 启动rpc服务节点，开始提供rpc远程网络调用服务
    void Run();

private:
    // 组合muduo库的事件循环对象
    muduo::net::EventLoop m_eventloop;
    
    // 添加ZKClient成员变量
    ZKClient m_zkclient;

    struct ServiceInfo
    {
        google::protobuf::Service *m_service; // 存储服务对象
        // 存储服务方法对应的服务对象
        //key是方法名称，value是方法描述信息
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor *> m_methodMap;
    };
    // 存储注册成功的服务对象和其服务方法的信息
    //key是服务名称，value是服务对象和服务方法的信息
    std::unordered_map<std::string, ServiceInfo> m_serviceMap;

    // 新的socket连接回调
    void OnConnection(const muduo::net::TcpConnectionPtr &);
    // 已建立连接用户的读写事件回调
    void OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *, muduo::Timestamp);
    
    void SendRpcResponse(const muduo::net::TcpConnectionPtr&,google::protobuf::Message*);
};