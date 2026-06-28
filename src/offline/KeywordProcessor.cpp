#include "offline/KeywordProcessor.h"
#include "common/DirectoryScanner.h"
#include "common/TextUtils.h"
#include "common/Utils.h"
#include "common/JiebaSingleton.h"
#include "common/Trie.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

//构造函数 加载停用词
KeywordProcessor::KeywordProcessor()
{
    _enStopWords=load_stop_words("../data/stopwords/en_stopwords.txt");
    _cnStopWords=load_stop_words("../data/stopwords/cn_stopwords.txt");
    std::cout<<"[Keyword] Loaded"<<_enStopWords.size()
             <<" English +"<<_cnStopWords.size()
             <<" Chinese stopwords"<<std::endl;
}

// 主程序
void KeywordProcessor::process(const std::string& cnDir,
                               const std::string& enDir)
{
    create_en_dict(enDir, "../data/index/dict_en.dat");
    build_en_index("../data/index/dict_en.dat", "../data/index/index_en.dat");
    create_cn_dict(cnDir, "../data/index/dict_cn.dat");
    build_cn_index("../data/index/dict_cn.dat", "../data/index/index_cn.dat");
}

void KeywordProcessor::create_en_dict(const std::string& dir,
                                      const std::string& outfile)
{
    // 1.扫描目录
    // vector<string> 装读目录读到的文件
    auto files=DirectoryScanner::scan(dir);
    std::cout<<"[Keyword] English files: "<<files.size()<<std::endl;

    // 2.统计词频
    std::map<std::string,int> freq;
    for(const auto& filepath:files){
        std::ifstream ifs(filepath);
        std::string line;
        while(std::getline(ifs,line)){
            std::string norm=TextUtils::normalize_english_line(line);
            std::istringstream iss(norm);
            std::string word;
            while(iss>>word){
                if(_enStopWords.count(word)>0){continue;}
                freq[word]++;
            }
        }
    }

    // 3.写入词典库
    std::ofstream ofs(outfile);
    for(const auto& [word,count]:freq){
        ofs<<word<<" "<<count<<"\n";
    }
    std::cout<<"[Keyword] English dict size："<<freq.size()<<std::endl;
}

void KeywordProcessor::build_en_index(const std::string& dict,
                    const std::string& index)
{
    // 1.读取字典，建立：首字母 -> 行号集合
    //std::map<char,std::set<int>> charIndex; //set 自动排序 + 去重
    // 1.读取字典，建立：前缀 -> 行号集合
    std::map<std::string,std::set<int>> rawIndex;//暂存全量前缀索引
    std::ifstream ifs(dict);
    std::string word;
    int freq;
    int lineNo=1;

    // 从输入流中读出一个string 再读出一个int
    // 分别给word 和 freq
    while(ifs>>word>>freq){
    //=====这个if是首字母 -> 行号版本======
    //    if(!word.empty()){
    //        char first=word[0];
    //        charIndex[first].insert(lineNo);
    //    }
    //=====这里是 前缀 -> 行号 版本======
        if(word.empty()){continue;}
        //对单个词进行全量前缀索引
        for(size_t len=1;len<=word.size();++len){
            rawIndex[word.substr(0,len)].insert(lineNo);
        }
        lineNo++;
    }

    // 扫描暂存的全量前缀索引，去掉冗余
    std::map<std::string,std::set<int>> prefixIndex;
    std::string lastKept; //上一个真正写入 prefixIndex 的 key
    for(auto& [pref,lines]:rawIndex){
        //长度为1的前缀必然保留
        if(pref.size()==1){
            prefixIndex[pref]=std::move(lines);
            lastKept=pref;//保留此时的前缀用于下一循环时比较
            continue;
        }

        // 与lastKept的行号进行比较
        if(prefixIndex[lastKept]!=lines){
            //行号集不同 -> 说明行号集缩小了 有分叉 -> 需保留
            prefixIndex[pref]=std::move(lines);
            lastKept=pref;//更新发生分叉的前缀
        }
        //如果行号集相同 -> 说明这个没有分叉，保留这个前缀是冗余的
    }

    // ===== 旧文本格式写入（已废弃，改用下面的 Trie 二进制序列化）=====
    // std::ofstream ofs(index);
    // for(const auto&[prefix,lines]:prefixIndex){
    //     ofs<<prefix;
    //     for(int line:lines) ofs<<" "<<line;
    //     ofs<<"\n";
    // }

    // ② 将去冗后的前缀索引构建成 Trie，二进制序列化写入
    Trie trie;
    for (const auto& [pref, lines] : prefixIndex) {
        trie.insertNode(pref, lines);
    }
    std::ofstream ofs(index, std::ios::binary);
    trie.serialize(ofs);
    std::cout << "[Keyword] English index keys: " << prefixIndex.size() << std::endl;
}


void KeywordProcessor::create_cn_dict(const std::string& dir,
                    const std::string& outfile)
{
    auto files=DirectoryScanner::scan(dir);
    std::cout<<"[Keyword] Chinese files: "<<files.size()<<std::endl;

    std::map<std::string,int> freq;

    for(const auto& filepath:files){
        std::ifstream ifs(filepath);
        //全文读入
        std::string content((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());

        std::vector<std::string> words;
        JiebaSingleton::getJieba().Cut(content,words); //MIX模式分词

        //统计词频
        for(const auto& word: words){
            if(_cnStopWords.count(word)>0){continue;}//停用词
            if(TextUtils::is_chinese_punctuation_or_space(word)){continue;}//标点 空白
            freq[word]++;
        }
    }

    //写入词典库
    std::ofstream ofs(outfile);
    for(const auto& [word,count]:freq){
        ofs<<word<<" "<<count<<"\n";
    }

    std::cout<<"[Keyword] Chinese dict size: "<<freq.size()<<std::endl;
}

void KeywordProcessor::build_cn_index(const std::string& dict,
                                      const std::string& index)
{
    std::map<std::string,std::set<int>> charIndex;

    std::ifstream ifs(dict);
    std::string word;
    int freq;
    int lineNo=1;

    while(ifs>>word>>freq){
        auto chars=TextUtils::split_utf8_characters(word);
        for(const auto& ch:chars){
            charIndex[ch].insert(lineNo);
        }
        lineNo++;
    }

    std::ofstream ofs(index);
    for(const auto& [ch,lines]:charIndex){
        ofs<<ch;
        for(int line:lines){
            ofs<<" "<<line;
        }
        ofs<<"\n";
    }

    std::cout<<"[Keyword] Chinese index keys: "<<charIndex.size()<<std::endl;
}
