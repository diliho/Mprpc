#include <iostream>
#include "user.pb.h"
#include "mprpcapplication.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "rpcerror.h" 
#include  <memory>

int main(int argc, char **argv)
{
    // 初始化框架
    MprpcApplication::Init(argc, argv);

    //使用智能指针管理MprpcChannel对象
     std::unique_ptr<MprpcChannel> channel(new MprpcChannel());
     //  使用 get() 方法获取原始指针创建 Stub
    fixbug::UserServiceRpc_Stub stub(channel.get());

    fixbug::LoginRequest request;
    request.set_name("111"); 
    request.set_pwd("222");    

    fixbug::LoginResponse response;
    MprpcController controller;

    // 调用远程rpc服务Login
    stub.Login(&controller, &request, &response, nullptr);

    // 处理响应
    if (controller.Failed()) {
       std::cout << "Login failed: " << controller.ErrorText() << std::endl;
    } else {
        if (response.success()) {
           std::cout << "Login success" << std::endl;
        } else {
            std::cout << "Login failed: " << response.result().errormsg() << std::endl;
        }
    }

    return 0;
}