#include "offline/KeywordProcessor.h"
#include "offline/PageProcessor.h"
#include "common/Config.h"

#include <exception>
#include <iostream>

int main()
{
    // 加载配置文件
    Config::instance().load("../conf/config.conf");
    auto& cfg = Config::instance();

    try{
        // 关键字推荐
        KeywordProcessor kw;
        kw.process(
            cfg.get("CORPUS_CN_DIR", "../data/corpus/CN"),
            cfg.get("CORPUS_EN_DIR", "../data/corpus/EN"));
        // 网页搜索
        PageProcessor pp;
        pp.process(
            cfg.get("CORPUS_WEBPAGES_DIR", "../data/corpus/webpages"));
    }catch(const std::exception& e){
        std::cerr<<"Error: "<<e.what()<<std::endl;
        return 1;
    }
    return 0;
}
