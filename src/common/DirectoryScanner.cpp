#include "common/DirectoryScanner.h"

#include <dirent.h>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <vector>

std::vector<std::string> DirectoryScanner::scan(const std::string& dir)
{
    std::vector<std::string> files;
    DIR* dp=opendir(dir.c_str());
    if(!dp){
        throw std::runtime_error("opendir failed: "+dir);
    }

    while(dirent* entry=readdir(dp)){
        std::string name=entry->d_name;
        if(name=="."||name==".."){
            continue;
        }
        files.push_back(dir+"/"+name);
    }
    closedir(dp);
    std::sort(files.begin(),files.end());
    return files;
}
