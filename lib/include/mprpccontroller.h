#pragma once
#include<string>
#include"google/protobuf/service.h"
#include"rpcerror.h"

//控制网络传输和序列化/反序列化中的错误
class MprpcController:public google::protobuf::RpcController
{
public:
    MprpcController();
    void Reset();
    bool Failed()const;
    std::string ErrorText()const;
    void SetFailed(const std::string& reason);
    
    // 新增错误码相关方法
    void SetFailed(const RpcError& error);
    const RpcError* GetError() const;
    
    void StartCancel() override;
    bool IsCanceled() const override;
    void NotifyOnCancel(google::protobuf::Closure* callback) override;

private:
    bool m_failed;                     // RPC方法执行过程的状态
    std::string m_errText;              // 原始错误文本
    std::unique_ptr<RpcError> m_rpc_error; // 新的错误码体系错误对象
};
