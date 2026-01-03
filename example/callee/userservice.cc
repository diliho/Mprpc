/*
rpc服务提供者
*/
#include <iostream>
#include <string>
#include "user.pb.h"
#include "mprpcapplication.h"
#include "mprpcprovider.h"
#include "rpcerror.h" 

class UserService : public fixbug::UserServiceRpc
{
public:
    bool Login_local(std::string name, std::string pwd)
    {
        LOG_INFO("doing login service");
        LOG_INFO("name:%s pwd:%s", name.c_str(), pwd.c_str());
        
        // 模拟业务逻辑错误
        if (name == "invalid" || pwd == "invalid") {
            return false;
        }
        return true;
    }
    
    bool Register_local(std::uint32_t id, std::string name, std::string pwd)
    {
        LOG_INFO("name:%s pwd:%s", name.c_str(), pwd.c_str());
        LOG_INFO("id:%d name:%s pwd:%s", id, name.c_str(), pwd.c_str());
        
        // 模拟业务逻辑错误
        if (id == 0 || name.empty() || pwd.empty()) {
            return false;
        }
        return true;
    }

    /*
    重写基类UserServiceRpc的虚函数 Login
    该函数会被框架调用
    1. 框架接收到rpc调用请求
    2. 框架调用该函数
    */
    void Login(::google::protobuf::RpcController *controller,
               const ::fixbug::LoginRequest *request,
               ::fixbug::LoginResponse *response,
               ::google::protobuf::Closure *done) override
    {
        // 框架给业务函数Login传入参数LoginRequest
        std::string name = request->name();
        std::string pwd = request->pwd();

        bool login_result = Login_local(name, pwd);

        // 把业务函数的执行结果设置到响应对象LoginResponse
        fixbug::ResultCode *code = response->mutable_result();
        if (login_result)
        {
            code->set_errorcode(0);
            code->set_errormsg("login success!");
        }
        else
        {
            // 使用新的错误码体系：业务层登录失败
            auto error_code = RpcErrorUtil::createBusinessError(BusinessErrorCode::BUSINESS_RULE_VIOLATION, 0, "MPRPC");
            RpcError error(error_code, "Invalid username or password");
            
            // 设置到控制器
            static_cast<MprpcController*>(controller)->SetFailed(error);
            
            // 同时设置到响应对象
            code->set_errorcode(static_cast<int>(BusinessErrorCode::BUSINESS_RULE_VIOLATION));
            code->set_errormsg("Invalid username or password");
        }
        response->set_success(login_result);

        // 执行回调操作
        done->Run();
    }

    void Register(::google::protobuf::RpcController* controller,
                 const ::fixbug::RegisterRequest* request,
                 ::fixbug::RegisterResponse* response,
                 ::google::protobuf::Closure* done)
    {
        uint32_t id = request->id();
        std::string name = request->name();
        std::string pwd = request->pwd();
        
        bool register_result = Register_local(id, name, pwd);

        if (!register_result) {
            // 使用新的错误码体系：业务层注册失败
            auto error_code = RpcErrorUtil::createBusinessError(BusinessErrorCode::BUSINESS_RULE_VIOLATION, 0, "MPRPC");
            RpcError error(error_code, "Invalid registration information");
            
            // 设置到控制器
            static_cast<MprpcController*>(controller)->SetFailed(error);
            
            // 同时设置到响应对象（兼容现有协议）
            response->mutable_result()->set_errorcode(static_cast<int>(BusinessErrorCode::BUSINESS_RULE_VIOLATION));
            response->mutable_result()->set_errormsg("Invalid registration information");
        } else {
            response->mutable_result()->set_errorcode(0);
            response->mutable_result()->set_errormsg("");
        }
        response->set_success(register_result);
        
        // 执行回调操作
        done->Run();
    }
};

int main(int argc, char **argv)
{
    /*框架初始化*/
    MprpcApplication::Init(argc, argv);

    RpcProvider provider;
    // 把UserService对象发布到rpc节点上
    provider.NotifyService(new UserService());

    // 启动rpc服务节点，开始提供rpc远程网络调用服务
    provider.Run();
}