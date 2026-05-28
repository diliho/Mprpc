#include <iostream>
#include <string>
#include <vector>
#include "friend.pb.h"
#include "mprpcapplication.h"
#include "mprpcprovider.h"
#include "logger.h"
#include "friendservice.h"

std::vector<std::string> FriendService::GetFriendlist_local(uint32_t userid)
{
    LOG_INFO("doing GetFriendlist service");
    LOG_INFO("userid:%d", userid);
    return {"friend1", "friend2", "friend3"};
}

void FriendService::GetFriendlist(::google::protobuf::RpcController* controller,
                                  const ::fixbug::FriendlistRequest* request,
                                  ::fixbug::FriendlistResponse* response,
                                  ::google::protobuf::Closure* done)
{
    uint32_t userid = request->userid();
    std::vector<std::string> friendlist = GetFriendlist_local(userid);

    response->mutable_result()->set_errorcode(0);
    response->mutable_result()->set_errormsg("");
    for (auto& name : friendlist)
    {
        response->add_friends(name);
    }

    done->Run();
}