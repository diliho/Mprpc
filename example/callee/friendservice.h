#ifndef FRIENDSERVICE_H
#define FRIENDSERVICE_H

#include <string>
#include <vector>
#include "friend.pb.h"
#include "mprpcprovider.h"

class FriendService : public fixbug::FriendServiceRpc
{
public:
    // 获取好友列表
    std::vector<std::string> GetFriendlist_local(uint32_t userid);

    // 重写基类FriendServiceRpc的虚函数
    void GetFriendlist(::google::protobuf::RpcController* controller,
                       const ::fixbug::FriendlistRequest* request,
                       ::fixbug::FriendlistResponse* response,
                       ::google::protobuf::Closure* done) override;
};

#endif // FRIENDSERVICE_H