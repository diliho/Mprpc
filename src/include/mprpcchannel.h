#pragma once
#include<google/protobuf/service.h>
#include<google/protobuf/descriptor.h>
#include<google/protobuf/message.h>
#include"logger.h"
#include"zookeeperutil.h"

class MprpcChannel:public google::protobuf::RpcChannel
{
public:
    MprpcChannel();
    void CallMethod(const google::protobuf::MethodDescriptor*method,
                    google::protobuf::RpcController*controller,
                    const google::protobuf::Message*request,
                    google::protobuf::Message*response,
                    google::protobuf::Closure*done);
private:
    ZKClient m_zkclient;
};