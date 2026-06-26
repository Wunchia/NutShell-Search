#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>


class KeywordService
{
public:
    KeywordService()=default;

    //加载数据文件
    void load(const std::string& dictEnPath,
        const std::string& idxEnPath,
        const std::string& dictCnPath,
        const std::string& idxCnPath);

    //========查询入口：检测中/英文后分发到对应分支=========
    std::string query(const std::string& keyword,int topK=5);

private:
    // ========== 工具函数 ===========
    // 计算编辑距离
    static int editDistance(const std::string& s1,const std::string& s2);

    //检测输入是否包含汉字
    static bool containsCJK(const std::string& s);

    // =========== 查询分支 =============
    std::string queryEnglish(const std::string& keyword,int topK);
    std::string queryChinese(const std::string& keyword,int topK);

    // ===========内部数据加载============
    // 通用函数：词典文件每行“word freq” -> vector
    static std::vector<std::pair<std::string,int>>
    loadDict(const std::string& path);

    // 通用函数：索引文件每行“key lineNo1 lineNo2 ...” -> map
    static std::map<std::string,std::set<int>>
    loadIndex(const std::string& path);

    // ============内存数据结构===========
    // 词典：按行号索引
    std::vector<std::pair<std::string,int>> _dictEn;
    std::vector<std::pair<std::string,int>> _dictCn;

    // 索引：key -> 行号集合
    std::map<std::string,std::set<int>> _idxEn;
    std::map<std::string,std::set<int>> _idxCn;
};
