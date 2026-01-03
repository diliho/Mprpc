/*
MprpcChannel::CallMethod 是客户端RPC调用的核心实现，它负责：
- 从方法描述符中提取服务名和方法名
- 序列化请求参数
- 构造RPC请求头
- 通过TCP网络发送请求到服务器
- 接收服务器响应
- 解析响应结果到响应对象
*/

#include "mprpcchannel.h"
#include "rpcheader.pb.h"
#include "mprpcapplication.h"
#include "rpcerror.h" 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include "mprpccontroller.h"
#include"logger.h"
#include"rpcerror.h"

// 构造函数：初始化ZKClient
MprpcChannel::MprpcChannel()
{
    // 只初始化一次ZKClient
    m_zkclient.Start();
    
    LOG_INFO("MprpcChannel: ZKClient initialized successfully");

}

void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
    google::protobuf::RpcController* controller,
    const google::protobuf::Message* request,
    google::protobuf::Message* response,
    google::protobuf::Closure* done)
{
    const google::protobuf::ServiceDescriptor* sd = method->service();
    std::string service_name = sd->name();
    std::string method_name = method->name();
    
  
    //send_rpc_str=header_size+rpc_header_str+args_str
    //header_size=4+rpc_header_str.size()+args_str.size() 因为header_size自己是uint32_t类型，所以是4个字节

    // 获取参数的序列化字符串长度
    uint32_t args_size = 0;
    std::string args_str;
    
    if (request->SerializeToString(&args_str))
    {
        args_size = args_str.size();
    }
    else
    {
        // 使用新的错误码体系：序列化请求失败
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::SERIALIZE_FAILED, 0, "MPRPC");
        RpcError error(error_code, "serialize request error!");
        static_cast<MprpcController*>(controller)->SetFailed(error);
       
        return;
    }
    
    // 定义rpc的请求header
    mprpc::RpcHeader rpcHeader;
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);
    rpcHeader.set_args_size(args_size);

    uint32_t header_size = 0;
    std::string rpc_header_str;
    
    // 序列化rpc的请求头
    if (rpcHeader.SerializeToString(&rpc_header_str))
    {
        header_size = rpc_header_str.size();
    }
    else
    {
        // 使用新的错误码体系：序列化请求头失败
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::SERIALIZE_FAILED, 0, "MPRPC");
        RpcError error(error_code, "serialize rpc header error!");
        static_cast<MprpcController*>(controller)->SetFailed(error);
      
        return;
    }
    
    // 组织待发送的rpc请求的字符串
    std::string send_rpc_str;
    send_rpc_str.insert(0, std::string((char*)&header_size, 4)); // header_size
    send_rpc_str += rpc_header_str; // rpcheader_str
    send_rpc_str += args_str; // args_str
    
    // 打印日志
  
    LOG_INFO("header_size: %d", header_size);
   

 
    LOG_INFO("service_name: %s", service_name.c_str());
  
    LOG_INFO("method_name: %s", method_name.c_str());
 
   
   
    // 使用tcp网络编程，完成rpc方法的远程调用
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1)
    {
        char errtxt[512] = {0};
        sprintf(errtxt, "create socket error! errno:%d", errno);
        // 使用新的错误码体系：创建socket失败
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::NETWORK_CONNECT_FAILED, errno, "MPRPC");
        RpcError error(error_code, errtxt);
        static_cast<MprpcController*>(controller)->SetFailed(error);
        return;
    }
    
    // 通过zookeeper获取rpcserver的消息
    std::string method_path = "/" + service_name + "/" + method_name;
    std::string host_data = m_zkclient.GetData(method_path.c_str());
    if (host_data == "")
    {
        std::string error="Error:Get data error";
        static_cast<MprpcController*>(controller)->SetFailed(error);
        return;
    }
    int idx = host_data.find(":");
    //解析ip和port
    if (idx == -1)
    {
        // 使用新的错误码体系：服务地址无效
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::SERVICE_DISCOVERY_FAILED, 0, "MPRPC");
        RpcError error(error_code, method_path + " address is invalid!");
        static_cast<MprpcController*>(controller)->SetFailed(error);
        return;
    }
    std::string ip=host_data.substr(0,idx);
    uint16_t port=atoi(host_data.substr(idx+1,host_data.size()-idx).c_str());

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port); 
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str()); 
    
    // 连接服务器
    if (connect(clientfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        close(clientfd);
        char errtxt[512] = {0};
        sprintf(errtxt, "connect error! errno:%d", errno);
        // 使用新的错误码体系：连接服务器失败
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::NETWORK_CONNECT_FAILED, errno, "MPRPC");
        RpcError error(error_code, errtxt);
        static_cast<MprpcController*>(controller)->SetFailed(error);
        return;
    }

    
    // 发送rpc请求
    if (send(clientfd, send_rpc_str.c_str(), send_rpc_str.size(), 0) == -1)
    {
        close(clientfd);
        char errtxt[512] = {0};
        sprintf(errtxt, "send error! errno:%d", errno);
        // 使用新的错误码体系：发送数据失败
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::MESSAGE_SEND_FAILED, errno, "MPRPC");
        RpcError error(error_code, errtxt);
        static_cast<MprpcController*>(controller)->SetFailed(error);
        return;
    }   
    
    // 接收rpc响应
    char recv_buf[1024] = {0};
    int recv_size = 0;
    if ((recv_size = recv(clientfd, recv_buf, sizeof(recv_buf), 0)) == -1)
    {
        close(clientfd);
        char errtxt[512] = {0};
        sprintf(errtxt, "recv error! errno:%d", errno);
        // 使用新的错误码体系：接收数据失败
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::MESSAGE_RECV_FAILED, errno, "MPRPC");
        RpcError error(error_code, errtxt);
        static_cast<MprpcController*>(controller)->SetFailed(error);
        return;
    }
    
    // 解析rpc响应
    std::string recv_str(recv_buf, recv_size);
    
    // 1. 读取header_size
    uint32_t response_header_size = 0;
    recv_str.copy((char *)&response_header_size, 4, 0);
    
    // 2. 读取header_str
    std::string response_rpc_header_str = recv_str.substr(4, response_header_size);
    mprpc::RpcHeader response_rpcHeader;
    if (!response_rpcHeader.ParseFromString(response_rpc_header_str))
    {
        close(clientfd);
        char errtxt[512] = {0};
        sprintf(errtxt, "parse rpc header error! header_str:%s", response_rpc_header_str.c_str());
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::DESERIALIZE_FAILED, 0, "MPRPC");
        RpcError error(error_code, errtxt);
        static_cast<MprpcController*>(controller)->SetFailed(error);
        return;
    }
    
    // 3. 读取response_str
    uint32_t response_args_size = response_rpcHeader.args_size();
    std::string response_str = recv_str.substr(4 + response_header_size, response_args_size);
    
    // 4. 解析response对象
    if (!response->ParseFromString(response_str))
    {
        close(clientfd);
        char errtxt[512] = {0};
        sprintf(errtxt, "parse error! response_str:%s", response_str.c_str());
        // 使用新的错误码体系：解析响应失败
        auto error_code = RpcErrorUtil::createFrameError(FrameErrorCode::DESERIALIZE_FAILED, 0, "MPRPC");
        RpcError error(error_code, errtxt);
        static_cast<MprpcController*>(controller)->SetFailed(error);
        return;
    }
    
    // 关闭socket
    close(clientfd);
}

