#include <iostream>
#include <string>
#include "user.pb.h"
#include "mprpcapplication.h"
#include "mprpcprovider.h"
#include "rpcerror.h"
#include "userservice.h"

bool UserService::Login_local(std::string name, std::string pwd)
{
    LOG_INFO("doing login service");
    LOG_INFO("name:%s pwd:%s", name.c_str(), pwd.c_str());

    if (name == "invalid" || pwd == "invalid") {
        return false;
    }
    return true;
}

bool UserService::Register_local(std::uint32_t id, std::string name, std::string pwd)
{
    LOG_INFO("name:%s pwd:%s", name.c_str(), pwd.c_str());
    LOG_INFO("id:%d name:%s pwd:%s", id, name.c_str(), pwd.c_str());

    if (id == 0 || name.empty() || pwd.empty()) {
        return false;
    }
    return true;
}

void UserService::Login(::google::protobuf::RpcController *controller,
                        const ::fixbug::LoginRequest *request,
                        ::fixbug::LoginResponse *response,
                        ::google::protobuf::Closure *done)
{
    std::string name = request->name();
    std::string pwd = request->pwd();

    bool login_result = Login_local(name, pwd);

    fixbug::ResultCode *code = response->mutable_result();
    if (login_result)
    {
        code->set_errorcode(0);
        code->set_errormsg("login success!");
    }
    else
    {
        auto error_code = RpcErrorUtil::createBusinessError(BusinessErrorCode::BUSINESS_RULE_VIOLATION, 0, "MPRPC");
        RpcError error(error_code, "Invalid username or password");
        static_cast<MprpcController*>(controller)->SetFailed(error);
        code->set_errorcode(static_cast<int>(BusinessErrorCode::BUSINESS_RULE_VIOLATION));
        code->set_errormsg("Invalid username or password");
    }
    response->set_success(login_result);
    done->Run();
}

void UserService::Register(::google::protobuf::RpcController* controller,
                           const ::fixbug::RegisterRequest* request,
                           ::fixbug::RegisterResponse* response,
                           ::google::protobuf::Closure* done)
{
    uint32_t id = request->id();
    std::string name = request->name();
    std::string pwd = request->pwd();

    bool register_result = Register_local(id, name, pwd);

    if (!register_result) {
        auto error_code = RpcErrorUtil::createBusinessError(BusinessErrorCode::BUSINESS_RULE_VIOLATION, 0, "MPRPC");
        RpcError error(error_code, "Invalid registration information");
        static_cast<MprpcController*>(controller)->SetFailed(error);
        response->mutable_result()->set_errorcode(static_cast<int>(BusinessErrorCode::BUSINESS_RULE_VIOLATION));
        response->mutable_result()->set_errormsg("Invalid registration information");
    } else {
        response->mutable_result()->set_errorcode(0);
        response->mutable_result()->set_errormsg("");
    }
    response->set_success(register_result);
    done->Run();
}