#pragma once
#include <cppjieba/Jieba.hpp>

class JiebaSingleton
{
public:
    static cppjieba::Jieba& getJieba(){
        static cppjieba::Jieba jieba;
        return jieba;
    }
    JiebaSingleton(const JiebaSingleton&)=delete;
    JiebaSingleton& operator=(const JiebaSingleton&)=delete;
private:
    JiebaSingleton()=default;
    ~JiebaSingleton()=default;
};
