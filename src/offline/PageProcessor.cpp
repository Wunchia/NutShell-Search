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

    int nextId=1;//doc.id是全局唯一的，需要定义在for之外保证跨文件也不重复

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
            //每轮重新构造doc 保证doc初始均为空
            Document doc;

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
            }

            //只有link、title、content都成功拿到了才会push_back
            if(doc.link.empty()||doc.title.empty()||doc.content.empty()){
                item=item->NextSiblingElement("item");
                continue;
            }
            //确定入库后再分配id并自增，避免遇到问题item导致id虚增
            doc.id=nextId++;
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
        //生成当前文档的指纹
        uint64_t hash=0;
        int topN=std::max(5,std::min(200,static_cast<int>(doc.content.size()/120)));
        _hasher.make(doc.content,topN,hash);
        //这里生成的topN关键词在make中用完即弃 后面TF-IDF会另外再生成

        // 检查是否与已有文档重复
        bool duplicated=false;
        for(size_t i=0;i<fingerprints.size();++i){
            //比较当前文档的hash和fingerprints中已保留的文档hash 如果汉明距离小于等3则判定为重复
            if(simhash::Simhasher::isEqual(hash, fingerprints[i])){
                duplicated=true;
                break;
            }
        }

        //如果不重复 则该篇文档存入uniqueDocs 指纹存入fingerprints
        if(!duplicated){
            uniqueDocs.push_back(std::move(doc));
            fingerprints.push_back(hash);
        }
    }
    //把去重后的文档放回_documents
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
        // 第一次循环时这里调用tellp拿到的是起始字节位置
        // 之后每次循环都刚好是下一篇的起始偏移
        std::streamoff offset=pageOfs.tellp();
        pageOfs<<page;//这里才把本篇文档传入网页库

        // 写入偏移记录：id offset length
        // 第一篇的起始偏移是0
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

    std::map<std::string,int> documentFrequency;          //word -> DF 包含该词的文档个数

    for(const auto& doc:_documents){
        std::vector<std::string> words;//存分词结果 所有词
        _tokenizer.Cut(doc.content,words);

        std::map<std::string,int> localFreq;//存词频 key唯一
        for(const auto& w:words){
            if(_stopWords.count(w)>0){continue;}//去停用词
            if(TextUtils::is_chinese_punctuation_or_space(w)){continue;}//去标点
            localFreq[w]++;
        }

        //存入docTermCount （TF 词语在文档中出现的次数）
        docTermCount[doc.id]=localFreq;
        //汇总总词数
        docTotalWords[doc.id]=0;
        for(const auto& [word,cnt]:localFreq){
            docTotalWords[doc.id]+=cnt;//累加词频 同一文档中同一词出现几次这里cnt等于几
            //记录包含该词的文档个数 一个词在同一文档中出现多次这里也只会++一次
            documentFrequency[word]++;
        }
    }

    // 第二遍：计算 TF-IDF 权重并归一化
    _invertedIndex.clear();

    for(const auto& doc : _documents){
        int totalWords=docTotalWords[doc.id];
        if(totalWords==0){continue;}

        const auto& localFreq=docTermCount[doc.id];

        // 先算原始TF-IDF权重
        std::map<std::string,double> rawWeights;
        double squareSum=0.0;

        for(const auto& [word,cnt]:localFreq){
            //TF=词语在文档中出现的次数/文档总词数  衡量一个词语在该篇文档中出现的频率高低 越高权重越高
            double tf=static_cast<double>(cnt)/totalWords;
            //IDF=log2(文档总数/包含该词语的文档数+1)
            // DF衡量一个词语在所有文档中出现的频率高低 越高权重越低
            // IDF则反过来 越高权重越高 所以相乘后的值越大权重越高
            double idf=std::log2(static_cast<double>(N)/(documentFrequency[word]+1));
            double weight=tf*idf;
            rawWeights[word]=weight;
            squareSum+=weight*weight;//求平方和
        }

        // 归一化
        double norm=std::sqrt(squareSum);//做分母
        for(const auto& [word,weight]:rawWeights){
            //读出前面的每一个关键词的权重 分别除以分母得到归一化的权重
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
