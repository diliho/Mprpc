#include <iostream>
#include <string>
#include "friend.pb.h"
#include "mprpcapplication.h"
#include "mprpcprovider.h"
#include"logger.h"
class FriendService:public fixbug::FriendServiceRpc
{
public:
    // 获取好友列表
    std::vector<std::string> GetFriendlist_local(uint32_t userid)
    {
  
    LOG_INFO("doing GetFriendlist service");
  
    LOG_INFO("userid:%d",userid);
    return {"friend1","friend2","friend3"};
    }

    //重写FriendListServiceRPC的虚函数GetFriendlist
    
void GetFriendlist(::google::protobuf::RpcController* controller,
                         const ::fixbug::FriendlistRequest*request,
                         ::fixbug::FriendlistResponse*response,
                         ::google::protobuf::Closure* done)
                         {
                             // 从request中获取userid
                             uint32_t userid=request->userid();
                             // 调用本地方法
                             std::vector<std::string> friendlist=GetFriendlist_local(userid);
                             // 将结果写入response
                             response->mutable_result()->set_errorcode(0);
                             response->mutable_result()->set_errormsg("");
                             // 将好友列表写入response
                             for(std::string& name:friendlist)
                             {
                                 std::string* friend_name=response->add_friends();
                                 *friend_name=name;
                             }
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
    provider.NotifyService(new FriendService());

    // 启动rpc服务节点，开始提供rpc远程网络调用服务
    provider.Run();
}