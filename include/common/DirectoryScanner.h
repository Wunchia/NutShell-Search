#pragma once

#include <string>
#include <vector>

class DirectoryScanner
{
public:
    static std::vector<std::string> scan(const std::string& dir);
private:
    DirectoryScanner()=delete;
};
