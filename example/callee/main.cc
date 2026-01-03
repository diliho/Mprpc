/*
RPC服务提供者主入口
同时提供userservice和friendservice
*/
#include <iostream>
#include "mprpcapplication.h"
#include "mprpcprovider.h"
#include "user.pb.h"
#include "friend.pb.h"
#include "userservice.h"  // 添加头文件引用
#include "friendservice.h"  // 添加头文件引用

int main(int argc, char **argv)
{
    /*框架初始化*/
    MprpcApplication::Init(argc, argv);

    RpcProvider provider;
    
    // 把UserService对象发布到rpc节点上
    provider.NotifyService(new UserService());
    
    // 把FriendService对象发布到rpc节点上
    provider.NotifyService(new FriendService());

    // 启动rpc服务节点，开始提供rpc远程网络调用服务
    provider.Run();
    
    return 0;
}