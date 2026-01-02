#pragma once
#include"mprpcconfig.h"
#include"mprpcchannel.h"
#include"mprpccontroller.h"
#include"mprpcprovider.h"
#include"zookeeperutil.h"


class MprpcApplication
{
public:
    static void Init(int argc, char **argv);
    // 获取单例对象
    static MprpcApplication &GetInstance();
    static MprpcConfig&GetConfig();
   

private:
    MprpcApplication() {};
    // 禁止拷贝构造函数
    MprpcApplication(const MprpcApplication &) = delete;
    // 禁止移动构造函数
    MprpcApplication(MprpcApplication &&) = delete;
    static  MprpcConfig m_config;
};
