#include "online/KeywordService.h"
#include "common/TextUtils.h"

#include <nlohmann/json.hpp>
#include <utfcpp/utf8.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

// ==============================================
//                   数据加载
// ==============================================
//loadDict
std::vector<std::pair<std::string,int>>
KeywordService::loadDict(const std::string& path){
    std::vector<std::pair<std::string,int>> dict;
    std::ifstream ifs(path);
    if(!ifs){
        throw std::runtime_error("Cannot open dict file: "+path);
    }
    std::string word;
    int freq=0;
    while(ifs>>word>>freq){
        dict.emplace_back(word,freq);
    }
    std::cout<<"[KeywordService] Loaded "<<path
        <<": "<<dict.size()<<" entries"<<std::endl;

    return dict;
}

//loadIndex
std::map<std::string,std::set<int>>
KeywordService::loadIndex(const std::string& path){
    std::map<std::string,std::set<int>> index;
    std::ifstream ifs(path);
    std::string line;
    while(std::getline(ifs,line)){
        std::istringstream iss(line);
        std::string key;
        //没拿到key 就continue
        if(!(iss>>key)){continue;}

        //把行号列表存进去
        int lineNo=0;
        while(iss>>lineNo){
            index[key].insert(lineNo);
        }
    }

    std::cout<<"[KeywordService] Loaded "<<path
             <<" : "<<index.size()<<" keys "<<std::endl;

    return index;
}

//load
void KeywordService::load(const std::string& dictEnPath,
                          const std::string& idxEnPath,
                          const std::string& dictCnPath,
                          const std::string& idxCnPath)
{
    _dictEn=loadDict(dictEnPath);
    _dictCn=loadDict(dictCnPath);
    _idxEn=loadIndex(idxEnPath);
    _idxCn=loadIndex(idxCnPath);
}

// =============================
//          编辑距离算法
// =============================
//     ""  a  b  c
// ""   0  1  2  3
// a    1  0  1  2
// b    2  1  0  1
// d    3  2  1  1   ← prev[3] = 1

// 常规解法 30行
// static int editDistance(const std::string& s1,const std::string& s2){
//     int n=s1.length();
//     int m=s2.length();

//     if(n*m==0) return n+m;

//     std::vector<std::vector<int>> D(n+1,std::vector<int>(m+1));

//     for(int i=0;i<n+1;++i){
//         D[i][0]=i;
//     }

//     for(int j=0;j<m+1;++j){
//         D[0][j]=j;
//     }

//     //计算所有DP值
//     for(int i=1;i<n+1;++i){
//         for(int j=1;j<m+1;++j){
//             int left=D[i-1][j]+1;
//             int up=D[i][j-1]+1;
//             int left_up=D[i-1][j-1];//替换一个字母
//             if(s1[i-1]!=s2[j-1]){
//                 left_up+=1;
//             }
//             D[i][j]=std::min(left,std::min(up,left_up));
//         }
//     }
//     return D[n][m];
// }

// 空间优化后 不用整个二维表 只用两行滚动求解
int KeywordService::editDistance(const std::string& s1,const std::string& s2)
{
    int m=s1.length();
    int n=s2.length();

    //prev 存上一行， curr存当前行
    std::vector<int> prev(n+1);
    std::vector<int> curr(n+1);

    //初始化第一行
    for(int j=0;j<n;++j){
        prev[j]=j;
    }

    //以当前行不断向后推 中间通过swap更新上一行
    //逐行填充
    for(int i=0;i<=m;++i){
        curr[0]=i;
        //处理一行
        for(int j=1;j<=n;++j){
            int del=prev[j]+1;
            int ins=curr[j-1]+1;
            int sub=prev[j-1];
            if(s1[i-1]!=s2[j-1]){
                sub+=1;
            }

            curr[j]=std::min(del,std::min(ins,sub));
        }

        std::swap(prev,curr);//curr变prev，新的curr在下一轮填充的过程中被覆盖
    }

    return prev[n];
}

//========================================
//          containsCJK
//========================================
bool KeywordService::containsCJK(const std::string& s){
    // utf8::iterator 在遍历时会自动跳过 UTF-8 多字节序列中的
    // 后续字节（continuation byte），每次只返回一个完整的 Unicode 码点
    auto it=utf8::iterator<std::string::const_iterator>(s.begin(),s.begin(),s.end());
    auto end=utf8::iterator<std::string::const_iterator>(s.end(),s.begin(),s.end());

    for(;it!=end;++it){
        char32_t cp=*it;

        // CJK 统一汉字基本区
        if(cp>=0x4E00&&cp<=0x9FFF){return true;}
        // CJK 扩展A区（生僻字，覆盖更全）
        if(cp>=0x3400&&cp<=0x4DBF){return true;}
    }
    return false;
}

// ====================================
//             query 入口
// ====================================
std::string KeywordService::query(const std::string& keyword,int topK)
{
    if(containsCJK(keyword)){
        return queryChinese(keyword,topK);
    }else{
        return queryEnglish(keyword,topK);
    }
}

//=====================queryEnglish=====================
std::string KeywordService::queryEnglish(const std::string& keyword,int topK)
{
    //① 前缀匹配：
    //    英文索引 index_en.dat 的 key 是「前缀」（去冗后仅保留分叉点），
    //    如 "a" "ap" "app" "appl"。用户输入 "app"，不会直接命中 "app" 本身，
    //    而是取最长匹配前缀 → 拿到其行号集合。
    //
    //    逐级回退策略：
    //      先试 keyword 全串 → 不命中则 keyword[0..n-1] → keyword[0..n-2]
    //      → ... 直到找到第一个存在的 key，或长度归 0
    //
    //    为什么不做二分查找 / lower_bound？
    //      → lower_bound 只能找到「第一个 ≥ keyword」的 key，
    //        但前缀索引去冗后，"apple" 的候选集可能挂在 "app" 下，
    //        而非 "apple" 本身。逐级回退更直观且 O(len(keyword))。
    std::set<int> candidateLines;
    {
        std::string prefix=keyword;
        while(!prefix.empty()){
            auto it=_idxEn.find(prefix);
            if(it!=_idxEn.end()){
                candidateLines=it->second;
                break;
            }
            prefix.pop_back();
        }
    }
    if(candidateLines.empty()){
        return "[]";//如果没有命中任何前缀，则返回空数组
    }

    // ② 取候选词：用行号去 _dictEn 取 (word, freq)
    //  _dictEn 是 vector<pair<string,int>>，行号从 1 开始
    // 存储三元组：(编辑距离, 词频, 词本身)
    //   - 词频存负值，便于直接用 tuple 默认比较（升序 = 编辑距离小→好，词频大→好）
    struct Candidate{
        int dist;//编辑距离 越小越好
        int freq;//词频 越大越好
        std::string word;//候选词本身 字典序 越小越好
    };
    std::vector<Candidate> Candidates;

    for(int lineNo:candidateLines){
        if(lineNo<1||static_cast<size_t>(lineNo)>_dictEn.size()){
            continue;
        }
        const auto& [word,freq]=_dictEn[lineNo-1];

        if(word==keyword){continue;}//跳过用户输入本身（原词）
        int dist=editDistance(keyword,word);
        Candidates.push_back({dist,freq,word});
    }

    // ③ 编辑距离排序 + 三级优先级
    //   ① 优先比较编辑距离（越小越相似）
    //   ② 编辑距离相同时，比较词频（越高越热门）
    //   ③ 词频也相同时，按字典序比较候选词（越小越靠前）
    // lambda 返回 true 表示 a 应该排在 b 前面
    std::sort(Candidates.begin(),Candidates.end(),
        [](const Candidate& a,const Candidate&b){
            if(a.dist!=b.dist){return a.dist<b.dist;}//编辑距离
            if(a.freq!=b.freq){return a.freq>b.freq;}//词频
            return a.word<b.word;//字典序
    });

    // ④ TopK → nlohmann::json → 序列化
    nlohmann::json result=nlohmann::json::array();
    size_t limit=std::min(static_cast<size_t>(topK),Candidates.size());
    for(size_t i=0;i<limit;++i){
        result.push_back(Candidates[i].word);
    }
    return result.dump();//回传字符串
}

//===========queryChinese=================
std::string KeywordService::queryChinese(const std::string& keyword,int topK)
{
    // ① 拆字：（复用一期离线建索引时的同一套拆字逻辑，保证一致性）
    auto chars=TextUtils::split_utf8_characters(keyword);

    // ② 查索引取并集：
    //    _idxCn 是 map<单个汉字, set<行号>>，离线部分按单字建索引。
    std::set<int> candidateLines;

    for(const auto& ch:chars){
        auto it=_idxCn.find(ch);
        if(it==_idxCn.end()){
            continue;
        }
        candidateLines.insert(it->second.begin(),it->second.end());
    }

    if(candidateLines.empty()){
        return "[]";
    }

    // ③ 取候选词 + 编辑距离排序 + 三级优先级
    struct Candidate{
        int dist;
        int freq;
        std::string word;
    };
    std::vector<Candidate> Candidates;

    for(int lineNo:candidateLines){
        if(lineNo<1||static_cast<size_t>(lineNo)>_dictCn.size()){
            continue;
        }
        const auto& [word,freq]=_dictCn[lineNo-1];

        if(word==keyword){continue;}//跳过用户输入本身（原词）
        int dist=editDistance(keyword,word);
        Candidates.push_back({dist,freq,word});
    }

    std::sort(Candidates.begin(),Candidates.end(),
        [](const Candidate& a,const Candidate& b){
            if(a.dist!=b.dist){return a.dist<b.dist;}
            if(a.freq!=b.freq){return a.freq<b.freq;}
            return a.word<b.word;
        }
    );

    // ④ TopK → JSON
    nlohmann::json result=nlohmann::json::array();
    size_t limit=std::min(static_cast<size_t>(topK),Candidates.size());
    for(size_t i=0;i<limit;++i){
        result.push_back(Candidates[i].word);
    }
    return result.dump();
}
