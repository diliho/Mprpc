#include "mprpcapplication.h"
#include <iostream>
#include <unistd.h>
#include <string>

MprpcConfig MprpcApplication::m_config;
std::shared_ptr<ZKClient> MprpcApplication::m_zkclient = nullptr;
std::once_flag MprpcApplication::m_zk_once_flag;

void ShowArgsHelp()
{
    
    std::cout << "format: command -i <configfile>" << std::endl;

}
void MprpcApplication::Init(int argc, char **argv)
{
    if (argc < 2)
    {
        ShowArgsHelp();
        exit(EXIT_FAILURE);
    }
    int c = 0;
    std::string config_file;

    // 解析命令行参数
    while ((c = getopt(argc, argv, "i:")) != -1)
    {
        switch (c)
        {
        case 'i':
            config_file = optarg;
            break;
        case '?':
            std::cout << "invalid args!" << std::endl;
            ShowArgsHelp();
            exit(EXIT_FAILURE);
        case ':':
            std::cout << "need <configfile>" << std::endl;
            ShowArgsHelp();
            exit(EXIT_FAILURE);
        default:
            break;
        }
    }

    // 加载配置文件 RpcServer的ip port zookeeper的ip port
    m_config.LoadConfigfile(config_file.c_str());
   
}
MprpcApplication &MprpcApplication::GetInstance()
{
    static MprpcApplication app;
    return app;
}
MprpcConfig &MprpcApplication::GetConfig()
{
    return m_config;
}

std::shared_ptr<ZKClient> MprpcApplication::GetZKClient()
{
    std::call_once(m_zk_once_flag, []() {
        m_zkclient = std::make_shared<ZKClient>();
        m_zkclient->Start();
    });
    return m_zkclient;
}