#include "common/Config.h"

#include <fstream>
#include <sstream>

Config& Config::instance()
{
    static Config c;
    return c;
}

void Config::load(const std::string& path)
{
    std::ifstream ifs(path);
    if(!ifs){return;}

    std::string line;
    while(std::getline(ifs,line)){
        size_t first=line.find_first_not_of(" \t\r");
        if(first==std::string::npos){continue;}
        if(line[first]=='#'){continue;}

        size_t eq=line.find('=',first);
        if(eq==std::string::npos){continue;}

        std::string key=line.substr(first,eq-first);
        std::string val=line.substr(eq+1);

        //去 value 尾部空格
        size_t end=val.find_last_not_of(" \t\r\n");
        if(end!=std::string::npos)val=val.substr(0,end+1);

        _map[key]=val;
    }
}

std::string Config::get(const std::string& key,
                        const std::string& defaultValue)const
{
    auto it=_map.find(key);
    return (it!=_map.end())?it->second:defaultValue;
}

int Config::getInt(const std::string& key,int defaultValue)const{
    auto it=_map.find(key);
    if(it==_map.end()){return defaultValue;}
    try{return std::stoi(it->second);}
    catch(...){return defaultValue;}
}
