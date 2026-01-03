#include<iostream>
#include"mprpcapplication.h"
#include"friend.pb.h"


int main(int argc,char **argv)
{

   MprpcApplication::Init(argc,argv);
   
   // 将MprpcChannel对象放在栈上创建
   MprpcChannel channel;
   fixbug::FriendServiceRpc_Stub stub(&channel);
   
   fixbug::FriendlistRequest request;
   request.set_userid(1000);
   
   fixbug::FriendlistResponse response;
    
   MprpcController controller;

   // ZKClient zkclient;
   // zkclient.Start();
   // std::string conststr=zkclient.GetData("/rpcserverip");
   // std::cout<<"conststr:"<<conststr<<std::endl;
   // std::vector<std::string> vec=StringSplit(conststr,':');
   // if(vec.size()!=2)
   // {
   //     std::cout<<"rpcserverip:rpcserverport is not exist"<<std::endl;
   //     exit(EXIT_FAILURE);

   // }
   // std::string ip=vec[0];

   stub.GetFriendlist(&controller,&request,&response,nullptr);

   //先判断controller
   if(controller.Failed())//失败
   {
       controller.ErrorText();
   }
   else
   {
    if(!response.result().errorcode())
   {
        LOG_INFO("rpc GetFriendList response friend size:%d",response.friends_size());
      std::cout<<"rpc GetFriendList response friend size:"<<response.friends_size()<<std::endl;
    
      for(int i=0;i<response.friends_size();i++)
      {
         
         std::cout<<"friend name:"<<response.friends(i)<<std::endl;
         
      }
   }
   else
   {
      std::cout<<"rpc GetFriendList response error :"<<response.result().errormsg()<<std::endl;
      LOG_ERROR("rpc GetFriendList response error :%s",response.result().errormsg().c_str());
   }
   }


   return 0;

}