#include"mprpcconfig.h"
#include<string>
void MprpcConfig::LoadConfigfile(const char*config_file)
{
    FILE*pf=fopen(config_file,"r");
    if(pf==nullptr)
    {
        printf("%s:%s:%d:open %s file is error!\n",__FILE__,__FUNCTION__,__LINE__,config_file);
        exit(EXIT_FAILURE);
    }

    
    while(!feof(pf))
    {char buf[512]={0};
    //读取一行
    fgets(buf,512,pf);

    //去掉字符串前面的多余空格
    std::string src_buf(buf);
    int idx=src_buf.find_first_not_of(' ');
    if(idx!=-1)
    {
        src_buf=src_buf.substr(idx,src_buf.size()-idx);
    }
    //判断#的注释
    if(src_buf[0]=='#'||src_buf.empty())
    {
        //注释或者空行
        continue;
    }
    //解析配置项
    idx=src_buf.find('=');
    if(idx==-1)
    {
        //配置项格式错误
        continue;
    }
    std::string key;
    std::string value;
    key=src_buf.substr(0,idx);
    // 去掉key后面的空格
    int end_idx = key.find_last_not_of(' ');
    if(end_idx != -1)
    {
        key = key.substr(0, end_idx + 1);
    }
    value=src_buf.substr(idx+1,src_buf.size()-idx);
    // 去掉value前面的空格
    int start_idx = value.find_first_not_of(' ');
    if(start_idx != -1)
    {
        value = value.substr(start_idx, value.size()-start_idx);
    }
    // 去掉value后面的空格和换行符
    end_idx = value.find_last_not_of(" \r\n"); 
    if(end_idx != -1)
    {
        value = value.substr(0, end_idx + 1);
    }
    m_configMap.insert({key,value});


    }
}

std::string MprpcConfig::Load(const std::string&key)
{
    auto it=m_configMap.find(key);
    if(it==m_configMap.end())//没找到
    {
    return "";
    }
    return it->second;

}