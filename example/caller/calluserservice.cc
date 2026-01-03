#include<iostream>
#include"mprpcapplication.h"
#include"user.pb.h"
#include"mprpcchannel.h"

//通过框架调用rpc服务
int main(int argc, char **argv)
{
    // 初始化框架
    MprpcApplication::Init(argc, argv);
    
    // 将MprpcChannel对象放在栈上创建
    MprpcChannel channel;
    fixbug::UserServiceRpc_Stub stub(&channel);
    
  /*
  调用远程login
  */

   //rpc方法的参数
   fixbug::LoginRequest request;
   request.set_name("111");
   request.set_pwd("222");

   //响应
   fixbug::LoginResponse response;
   
   //发起调用
   stub.Login(nullptr,&request,&response,nullptr);

   if(!response.result().errorcode())
   {
      std::cout<<"rpc login response succsess "<<response.success()<<std::endl;

   }
   else
   {
      std::cout<<"rpc login response error :"<<response.result().errormsg()<<std::endl;

   }

   /*
   调用远程register
   */
   fixbug::RegisterRequest request2;
   request2.set_id(2000);
   request2.set_name("zhang san");
   request2.set_pwd("333");
   //响应
   fixbug::RegisterResponse response2;

   //发起调用
   stub.Register(nullptr,&request2,&response2,nullptr);
   if(!response2.result().errorcode())
   { 
      LOG_INFO("rpc register response succsess %d",response2.success());
      std::cout<<"rpc register response succsess "<<response2.success()<<std::endl;
     
   }
   else
   {
      std::cout<<"rpc register response error :"<<response2.result().errormsg()<<std::endl;
      LOG_ERROR("rpc register response error :%s",response2.result().errormsg().c_str());
   }  
   


return 0;


   

}