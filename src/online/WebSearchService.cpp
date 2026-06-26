#include "online/WebSearchService.h"
#include "common/TextUtils.h"
#include "common/Utils.h"
#include "common/JiebaSingleton.h"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utfcpp/utf8/unchecked.h>
#include <utility>
#include <vector>

// ====================================
//            构造函数
// ====================================
WebSearchService::WebSearchService()
{
    _stopWords=load_stop_words("../data/stopwords/cn_stopwords.txt");
    std::cout<<"[WebSearch] Loaded "<<_stopWords.size()
        <<" stopwords "<<std::endl;
}

// =====================================
//             load：加载离线数据
// =====================================
void WebSearchService::load(const std::string& invertPath,
    const std::string& pagesPath,
    const std::string& offsetsPath)
{
    {
        // 加载倒排索引
        // word docId1 weight1 docId2 weight2 ...
        std::ifstream ifs(invertPath);
        if(!ifs){
            throw std::runtime_error("Cannot open: "+invertPath);
        }
        std::string line;
        while(std::getline(ifs,line)){
            std::istringstream iss(line);
            std::string word;
            if(!(iss>>word)){continue;}

            // 读取（docId,weight）对，直到行尾
            int docId=0;
            double weight=0.0;
            while(iss>>docId>>weight){
                _invertedIndex[word][docId]=weight;
            }
        }
        std::cout<<"[WebSearch] Loaded inverted index: "
            <<_invertedIndex.size()<<" keywords "<<std::endl;
    }

    {
        // 加载网页偏移库
        // docId offset length
        std::ifstream ifs(offsetsPath);
        if(!ifs){
            throw std::runtime_error("Cannot open: "+offsetsPath);
        }
        int docId=0;
        std::streamoff pos=0;
        size_t len=0;
        while(ifs>>docId>>pos>>len){
            _offsets[docId]={pos,len};
        }
        _docCount=static_cast<int>(_offsets.size());
        std::cout<<"[WebSearch] Loaded offsets: "
            <<_docCount<<" documents"<<std::endl;
    }

    //保存网页库路径
    _pagesPath=pagesPath;
}

// =====================================
//            query：查询入口
// =====================================
std::string WebSearchService::query(const std::string& queryText,int topK)
{
    if(_invertedIndex.empty()||_offsets.empty()){
        return "[]";
    }
    // ① 将用户输入视为一篇新文档，用 cppjieba 分词 + 停用词过滤
    std::vector<std::string> rawWords;
    JiebaSingleton::getJieba().Cut(queryText,rawWords);

    //---统计查询中的词频
    std::map<std::string,int> queryTermFreq;
    int queryTotalWords=0;
    for(const auto& w:rawWords){
        if(_stopWords.count(w)>0){continue;} //去停用词
        if(TextUtils::is_chinese_punctuation_or_space(w)){continue;} //去标点
        queryTermFreq[w]++;
        queryTotalWords++;
    }
    if(queryTermFreq.empty()){
        return "[]";
    }

    // ② 计算查询向量的 TF-IDF 权重（查倒排索引获取 DF）
    // 公式离线数据建库：
    //   TF  = count / queryTotalWords
    //   IDF = log2(N / (DF + 1)) 其中 DF = 倒排索引中该词出现的文档数
    //   raw = TF * IDF
    //   归一化 = raw / sqrt(Σ raw²)
    std::map<std::string,double> queryWeights;
    double squareSum=0.0;
    int N=_docCount;

    for(const auto& [word,cnt]:queryTermFreq){
        auto it=_invertedIndex.find(word);
        if(it==_invertedIndex.end()){
            continue;
        }
        int DF=static_cast<int>(it->second.size());

        double tf=static_cast<double>(cnt)/queryTotalWords;
        double idf=std::log2(static_cast<double>(N)/(DF+1));
        double raw=tf*idf;

        queryWeights[word]=raw;
        squareSum+=raw*raw;
    }

    if(queryWeights.empty()){
        return "[]";//所有查询词都不在倒排索引中
    }

    //---归一化查询向量
    double norm=std::sqrt(squareSum);
    for(auto& [word,w]:queryWeights){
        w/=norm;
    }

    // ③ 查倒排索引，取「包含全部关键词」的文档
    // 做法：取每个词对应的文档集合 → 求交集
    std::set<int> matchingDocs;
    bool first=true;

    for(const auto& [word,_]:queryWeights){
        auto it=_invertedIndex.find(word);
        if(it==_invertedIndex.end()){
            return "[]";
        }

        const auto& docMap=it->second;

        if(first){
            //第一个词：直接覆盖
            for(const auto& [docId,_]:docMap){
                matchingDocs.insert(docId);
            }
            first=false;
        }else{
            //后续词：取交集
            std::set<int> newIntersection;
            for(const auto&[docId,_]:docMap){
                if(matchingDocs.count(docId)>0){
                    newIntersection.insert(docId);
                }
            }
            matchingDocs=std::move(newIntersection);
        }

        if(matchingDocs.empty()){
            break;//交集已空 则提前退出
        }
    }

    if(matchingDocs.empty()){
        return "[]"; //没有文档同时包含所有查询词
    }

    // ④ 对每篇匹配文档，用倒排中的权重组成向量 Y，计算余弦相似度 cosθ = X·Y
    // （离线数据已做归一化，|X|=|Y|=1，cosθ 直接等于点积）
    struct DocScore{
        int docId;
        double score; //余弦相似度
    };
    std::vector<DocScore> scoredDocs;

    for(int docId:matchingDocs){
        double dotProduct=0.0;
        for(const auto&[word,qWeight]:queryWeights){
            auto it=_invertedIndex.find(word);
            if(it==_invertedIndex.end()){continue;}

            const auto& docMap=it->second;
            auto dit=docMap.find(docId);
            if(dit!=docMap.end()){
                dotProduct+=qWeight*dit->second;
            }
        }
        scoredDocs.push_back({docId,dotProduct});
    }

    // ⑤ 按余弦值降序排序
    std::sort(scoredDocs.begin(),scoredDocs.end(),
        [](const DocScore& a,const DocScore&b){
            return a.score>b.score;//降序: 相似度高的在前
        });

    // ⑥ 读取 topK 篇文档 → 提取 id/title/link → 生成摘要 → JSON
    nlohmann::json result=nlohmann::json::array();
    size_t limit=std::min(static_cast<size_t>(topK),scoredDocs.size());

    for(size_t i=0;i<limit;++i){
        int docId=scoredDocs[i].docId;

        //从 pages.dat 中按偏移读取文档内容
        PageInfo page=readPage(docId);

        nlohmann::json item;
        item["id"]=page.id;
        item["title"]=page.title;
        item["link"]=page.link;
        item["abstract"]=generateAbstract(page.content); //前100字摘要
        item["score"]=scoredDocs[i].score; //调试用 余弦相似度

        result.push_back(item);
    }

    std::cout<<"[WebSearch] query(\""<<queryText
        <<"\") -> "<<scoredDocs.size()<<" matches,top "
        <<limit<<std::endl;

    return result.dump();
}

// ============================================
//           readPage：按偏移读取单篇文档
// ============================================
WebSearchService::PageInfo WebSearchService::readPage(int docId) const
{
    PageInfo info;
    info.id=docId;

    auto it=_offsets.find(docId);
    if(it==_offsets.end()){
        return info;
    }

    const auto& off=it->second;

    //打开文件 -> seek -> 读
    std::ifstream ifs(_pagesPath,std::ios::binary);
    if(!ifs){
        return info;
    }
    ifs.seekg(off.pos);
    std::string xml(off.len,'\0');
    ifs.read(&xml[0],off.len);

    tinyxml2::XMLDocument doc;
    if(doc.Parse(xml.c_str(),xml.size())!=tinyxml2::XML_SUCCESS){
        return info;
    }

    auto* docEl=doc.FirstChildElement("doc");
    if(!docEl){return info;}

    auto* idEl = docEl->FirstChildElement("id");
    if (idEl && idEl->GetText()) {
        info.id = std::stoi(idEl->GetText());
    }

    auto* linkEl = docEl->FirstChildElement("link");
    if (linkEl && linkEl->GetText()) {
        info.link = linkEl->GetText();
    }

    auto* titleEl = docEl->FirstChildElement("title");
    if (titleEl && titleEl->GetText()) {
        info.title = titleEl->GetText();
    }

    auto* contentEl = docEl->FirstChildElement("content");
    if (contentEl && contentEl->GetText()) {
        info.content = contentEl->GetText();
    }

    return info;
}

// ==============================================
//          generateAbstract:静态摘要生成
// ==============================================
std::string WebSearchService::generateAbstract(const std::string& content,size_t maxLen)
{
    if(content.empty()){return "";}

    const char* curr=content.c_str();
    const char* end=content.c_str()+content.size();

    size_t charCount=0;
    while(curr<end&&charCount<maxLen){
        utf8::unchecked::next(curr);
        charCount++;
    }

    std::string abstract(content.c_str(),curr-content.c_str());

    //摘要因字数限制被截断时，在末尾加上省略号提示
    if(static_cast<size_t>(curr-content.c_str())<content.size()){
        abstract+=" ... ";
    }

    return abstract;
}
