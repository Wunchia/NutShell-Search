#include "offline/KeywordProcessor.h"
#include "offline/PageProcessor.h"

#include <exception>
#include <iostream>

int main()
{
    try{
        // 关键字推荐
        KeywordProcessor kw;
        kw.process("../data/corpus/CN","../data/corpus/EN");
        // 网页搜索
        PageProcessor pp;
        pp.process("../data/corpus/webpages");
    }catch(const std::exception& e){
        std::cerr<<"Error: "<<e.what()<<std::endl;
        return 1;
    }
    return 0;
}
