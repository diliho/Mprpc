#pragma once
#include<unordered_map>
#include<string>

class MprpcConfig
{
public:
//解析加载配置文件
    void LoadConfigfile(const char*config_file);
//查询配置
    std::string Load(const std::string&key);
//设置配置（运行时覆盖）
    void SetConfig(const std::string& key, const std::string& value);

private:
std::unordered_map<std::string,std::string> m_configMap;

};