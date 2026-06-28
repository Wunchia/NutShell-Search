#pragma once

#include <string>
#include <unordered_map>

// 简易配置读取器：逐行 key=value，忽略 # 注释和空行
// 用法：Config::instance().load("conf/config.conf");
//       Config::instance().get("LOG_LEVEL", "INFO");

class Config
{
public:
    static Config& instance();
    void load(const std::string& path);
    std::string get(const std::string& key,
        const std::string& defaultValue="")const;
    int getInt(const std::string& key,int defaultValue=0)const;

private:
    Config()=default;
    std::unordered_map<std::string, std::string> _map;
};
