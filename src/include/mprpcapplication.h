#pragma once
#include"mprpcconfig.h"
#include"mprpcchannel.h"
#include"mprpccontroller.h"
#include"mprpcprovider.h"
#include"zookeeperutil.h"
#include<memory>
#include<mutex>


class MprpcApplication
{
public:
    static void Init(int argc, char **argv);
    // 获取单例对象
    static MprpcApplication &GetInstance();
    static MprpcConfig&GetConfig();
    // 获取共享的 ZKClient（所有 MprpcChannel 共用，避免连接风暴）
    static std::shared_ptr<ZKClient> GetZKClient();

private:
    MprpcApplication() {};
    // 禁止拷贝构造函数
    MprpcApplication(const MprpcApplication &) = delete;
    // 禁止移动构造函数
    MprpcApplication(MprpcApplication &&) = delete;
    static  MprpcConfig m_config;
    static std::shared_ptr<ZKClient> m_zkclient;
    static std::once_flag m_zk_once_flag;
};
