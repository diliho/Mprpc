#pragma once
#include<string>
#include"google/protobuf/service.h"

//控制网络传输和序列化/反序列化中的错误
class MprpcController:public google::protobuf::RpcController
{
public:
    MprpcController();
    void Reset();
    bool Failed()const;
    std::string ErrorText()const;
    void SetFailed(const std::string& reason);
    
    
    void StartCancel() override;
    bool IsCanceled() const override;
    void NotifyOnCancel(google::protobuf::Closure* callback) override;

private:
  bool m_failed;//RPC方法执行过程的状态 
  std::string m_errText;

};
