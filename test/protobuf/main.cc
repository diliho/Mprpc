#include "test.pb.h"
#include <iostream>
using namespace fixbug;
#include <string>
int main()
{

    LoginRequest req;
    req.set_name("zhangsan");
    req.set_pwd("123456");

    // 对象数据序列化
    std::string send_str;
    if (req.SerializeToString(&send_str))
    {
        std::cout << send_str << std::endl;
    }

    // 反序列化
    LoginRequest reqB;
    if (reqB.ParseFromString(send_str))
    {
        std::cout << reqB.name() << std::endl;
        std::cout << reqB.pwd() << std::endl;
    }
    std::cout << "--------------------------------------" << std::endl;

    LoginResponse resp;
    resp.set_success(true);
    ResponseCode *code = resp.mutable_code();
    code->set_errorcode(1);

    code->set_errormsg("ok");
    std::string send_rsp;
    if (resp.SerializeToString(&send_rsp))
    {
        std::cout << send_rsp << std::endl;
    }

    std::cout << "--------------------------------------" << std::endl;

    GetFriendListResponse list_rsp;
    User *user = list_rsp.add_friends();
    user->set_name("user");
    user->set_age(20);
    user->set_sex(User::MAN);

    User *user2 = list_rsp.add_friends();
    user2->set_name("user2");
    user2->set_age(20);
    user2->set_sex(User::MAN);

    std::string send_list;
    if (list_rsp.SerializeToString(&send_list))
    {
        std::cout << send_list << std::endl;
        std::cout << list_rsp.friends_size() << std::endl;
    }

    return 0;
}