#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

class KeywordProcessor
{
public:
    KeywordProcessor();
    void process(const std::string& cnDir,const std::string& enDir);

private:
    void create_cn_dict(const std::string& dir,const std::string& outfile);
    void build_cn_index(const std::string& dict,const std::string& index);
    void create_en_dict(const std::string& dir,const std::string& outfile);
    void build_en_index(const std::string& dict,const std::string& index);

private:
    std::set<std::string> _enStopWords;
    std::set<std::string> _cnStopWords;
};
