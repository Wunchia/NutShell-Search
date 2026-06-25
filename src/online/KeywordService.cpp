#include "online/KeywordService.h"
#include "common/TextUtils.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

//loadDict
std::vector<std::pair<std::string,int>>
KeywordService::loadDict(const std::string& path){
    std::vector<std::pair<std::string,int>> dict;
    std::ifstream ifs(path);
    if(!ifs){
        std::cout<<"dict file open failed!"<<std::endl;
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

//query
std::string KeywordService::query(const std::string& keyword,int topK)
{
    std::cout << "[Keyword] editDistance(\"" << keyword
                 << "\", \"" << keyword.substr(0, keyword.size()-1)
                 << "\") = " << editDistance(keyword, keyword.substr(0, keyword.size()-1))
                 << std::endl;
    (void)topK;
    return "[\""+keyword+"\"]";
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
