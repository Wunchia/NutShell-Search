#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

class WebSearchService
{
public:
    WebSearchService();
    ~WebSearchService()=default;

    //加载离线数据：倒排索引、偏移库、网页库
    void load(const std::string& invertPath,
              const std::string& pagesPath,
              const std::string& offsetsPath);

    //查询入口：queryText=用户输入，topK=返回条数（默认10）
    std::string query(const std::string& queryText,int topK=10);

private:
    //============================
    //        数据结构
    //============================

    //倒排索引: 关键词 -> (文档id -> 归一化后的 TF-IDF 权重)
    std::map<std::string,std::map<int,double>> _invertedIndex;

    int _docCount=0;//文档总数 用于查询向量的IDF计算

    struct Offset{
        std::streamoff pos; //在 pages.dat中的起始字节偏移
        size_t len; //文档占多少字节
    };
    std::map<int,Offset> _offsets;

    // 网页库文件路径（不把全部网页 load 进内存，按需 seek 读取）
    std::string _pagesPath;

    //============================
    //          工具
    //============================

    std::set<std::string> _stopWords; //停用词

    //============================
    //        内部函数
    //============================

    // 读取单篇文档： seek到offset位置，读取len字节，解析xml
    struct PageInfo{
        int id;
        std::string link;
        std::string title;
        std::string content;
    };
    PageInfo readPage(int docId) const;

    // 生成静态摘要： 取content前maxLen个字符（按UTF-8字符计算）
    static std::string generateAbstract(const std::string& content,
        const std::map<std::string,double>& queryWords,
        size_t maxLen=100);
};
