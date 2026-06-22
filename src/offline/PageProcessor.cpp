#include "offline/PageProcessor.h"
#include "common/DirectoryScanner.h"
#include "common/TextUtils.h"
#include "common/Utils.h"

#include <algorithm>
#include <ios>
#include <simhash/Simhasher.hpp>
#include <tinyxml2.h>

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

//========= 构造 =========
PageProcessor::PageProcessor()
:_tokenizer()
,_hasher()
{
    _stopWords=load_stop_words("../data/stopwords/cn_stopwords.txt");
    // 网页搜索也用中文停用词（中文网页语料）
    std::cout<<"[Page] Loaded "<<_stopWords.size()<<" stopwords "<<std::endl;
}

//========== 入口============
void PageProcessor::process(const std::string& dir)
{
    extract_documents(dir);
    deduplicate_documents();
    build_pages_and_offsets("../data/index/pages.dat", "../data/index/offsets.dat");
    build_inverted_index("../data/index/invert_index.dat");
}

//============ XML解析 =============
void PageProcessor::extract_documents(const std::string& dir)
{
    auto files=DirectoryScanner::scan(dir);
    std::cout<<"[Page] XML files: "<<files.size()<<std::endl;

    int nextId=1;

    for(const auto& filepath : files){
        tinyxml2::XMLDocument xml;
        if(xml.LoadFile(filepath.c_str())!=tinyxml2::XML_SUCCESS){
            std::cerr<<"[Page] Failed to load: "<<filepath<<std::endl;
            continue;
        }

        auto* rss=xml.FirstChildElement("rss");
        if(!rss){continue;}
        auto* channel=rss->FirstChildElement("channel");
        if(!channel){continue;}

        auto* item=channel->FirstChildElement("item");
        while(item){
            Document doc;
            doc.id=nextId++;

            //提取link
            auto* linkEl=item->FirstChildElement("link");
            if(linkEl&&linkEl->GetText()){
                doc.link=linkEl->GetText();
            }

            //提取 title
            auto* titleEl=item->FirstChildElement("title");
            if(titleEl&&titleEl->GetText()){
                doc.title=titleEl->GetText();
            }

            // 提取content / description
            auto* contentEl=item->FirstChildElement("content");
            auto* descEl=item->FirstChildElement("description");

            if(contentEl&&contentEl->GetText()){
                doc.content=contentEl->GetText();
            }else if(descEl&&descEl->GetText()){
                doc.content=descEl->GetText();
            }else{
                item=item->NextSiblingElement("item");
                continue;
            }

            _documents.push_back(std::move(doc));
            item=item->NextSiblingElement("item");
        }
    }
    std::cout<<"[Page] Raw documents: "<<_documents.size()<<std::endl;
}

//============= SimHash去重 ==============
void PageProcessor::deduplicate_documents()
{
    if(_documents.empty()){return;}

    std::vector<Document> uniqueDocs;
    std::vector<uint64_t> fingerprints;

    for(auto& doc:_documents){
        uint64_t hash=0;
        int topN=std::max(5,std::min(200,static_cast<int>(doc.content.size()/120)));
        _hasher.make(doc.content,topN,hash);

        // 检查是否与已有文档重复
        bool duplicated=false;
        for(size_t i=0;i<fingerprints.size();++i){
            if(simhash::Simhasher::isEqual(hash, fingerprints[i])){
                duplicated=true;
                break;
            }
        }

        if(!duplicated){
            uniqueDocs.push_back(std::move(doc));
            fingerprints.push_back(hash);
        }
    }

    _documents=std::move(uniqueDocs);
    std::cout<<"[Page] Unique documents: "<<_documents.size()<<std::endl;
}

//=========== 网页库 + 偏移库 ============
void PageProcessor::build_pages_and_offsets(const std::string& pages,
                                            const std::string& offsets)
{
    std::ofstream pageOfs(pages,std::ios::binary);
    std::ofstream offsetOfs(offsets);

    for(const auto& doc: _documents){
        // 序列化为 XML 格式
        std::ostringstream oss;
        oss<<"<doc>\n"
            <<" <id>"<<doc.id<<"</id>\n"
            <<" <link>"<<doc.link<<"</link>\n"
            <<" <title>"<<doc.title<<"</title>\n"
            <<" <content>"<<doc.content<<"</content>\n"
            <<"</doc>\n";

        std::string page=oss.str();

        // 记录当前位置（偏移量）
        std::streamoff offset=pageOfs.tellp();
        pageOfs<<page;

        // 写入偏移记录：id offset length
        offsetOfs<<doc.id<<" "<<offset<<" "<<page.size()<<"\n";
    }
}

// ============== 倒排索引(TF-IDF) ================
void PageProcessor::build_inverted_index(const std::string&filename)
{
    int N=static_cast<int>(_documents.size());

    // 第一遍：统计每篇文档的词频和文档频率
    std::map<int,std::map<std::string,int>> docTermCount; //docId -> (word->count)
    std::map<int,int> docTotalWords;                      //docId -> 总词数
    std::map<std::string,int> documentFrequency;          //word -> DF

    for(const auto& doc:_documents){
        std::vector<std::string> words;
        _tokenizer.Cut(doc.content,words);

        std::map<std::string,int> localFreq;
        for(const auto& w:words){
            if(_stopWords.count(w)>0){continue;}
            if(TextUtils::is_chinese_punctuation_or_space(w)){continue;}
            localFreq[w]++;
        }

        docTermCount[doc.id]=localFreq;
        docTotalWords[doc.id]=0;
        for(const auto& [word,cnt]:localFreq){
            docTotalWords[doc.id]+=cnt;
            documentFrequency[word]++;
        }
    }

    // 第二遍：计算 TF-IDF 权重并归一化
    _invertedIndex.clear();

    for(const auto& doc : _documents){
        int totalWords=docTotalWords[doc.id];
        if(totalWords==0){continue;}

        const auto& localFreq=docTermCount[doc.id];

        // 先算原始权重
        std::map<std::string,double> rawWeights;
        double squareSum=0.0;

        for(const auto& [word,cnt]:localFreq){
            double tf=static_cast<double>(cnt)/totalWords;
            double idf=std::log2(static_cast<double>(N)/(documentFrequency[word]+1));
            double weight=tf*idf;
            rawWeights[word]=weight;
            squareSum+=weight*weight;
        }

        // 归一化
        double norm=std::sqrt(squareSum);
        for(const auto& [word,weight]:rawWeights){
            double normalized=(norm>0)?weight/norm:0.0;
            _invertedIndex[word][doc.id]=normalized;
        }
    }

    // 写入倒排索引文件
    std::ofstream ofs(filename);
    for(const auto& [word,docMap]:_invertedIndex){
        ofs<<word;
        for(const auto& [docId,weight]:docMap){
            ofs<<" "<<docId<<" "<<weight;
        }
        ofs<<"\n";
    }

    std::cout<<"[Page] Inverted index keywords: "<<_invertedIndex.size()<<std::endl;
}
