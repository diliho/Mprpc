#ifndef USERSERVICE_H
#define USERSERVICE_H

#include <string>
#include <vector>
#include "user.pb.h"
#include "mprpcprovider.h"

class UserService : public fixbug::UserServiceRpc
{
public:
    bool Login_local(std::string name, std::string pwd);
    bool Register_local(std::uint32_t id, std::string name, std::string pwd);

    // 重写基类UserServiceRpc的虚函数
    void Login(::google::protobuf::RpcController *controller,
               const ::fixbug::LoginRequest *request,
               ::fixbug::LoginResponse *response,
               ::google::protobuf::Closure *done) override;

    void Register(::google::protobuf::RpcController* controller,
                  const ::fixbug::RegisterRequest* request,
                  ::fixbug::RegisterResponse* response,
                  ::google::protobuf::Closure* done) override;
};

#endif // USERSERVICE_H