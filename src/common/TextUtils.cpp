#include "common/TextUtils.h"

#include <cctype>
#include <utfcpp/utf8.h>
#include <utfcpp/utf8/checked.h>

namespace TextUtils
{
    bool is_ascii_alpha(char ch){
        return (ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z');
    }

    std::string normalize_english_line(const std::string& line)
    {
        std::string result;
        for(char ch:line){
            if(is_ascii_alpha(ch)){
                result+=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }else{
                result+=' ';
            }
        }
        return result;
    }

    std::vector<std::string> split_utf8_characters(const std::string& word){
        std::vector<std::string> chars;
        const char* curr=word.c_str();
        const char* end=word.c_str()+word.size();

        while(curr!=end){
            const char* start=curr;
            utf8::next(curr,end);
            chars.emplace_back(start,curr);
        }
        return chars;
    }

    bool is_chinese_punctuation_or_space(const std::string &token)
    {
        if(token.empty()){return true;}

        if(token.size()==1&&std::isspace(static_cast<unsigned char>(token[0]))){
            return true;
        }

        if(token.size()==1&&!std::isalpha(static_cast<unsigned char>(token[0]))){
            return true;
        }

        auto it=utf8::iterator<std::string::const_iterator>(
            token.begin(),token.begin(),token.end()
        );

        char32_t cp=*it;

        // CJK 标点符号范围 (U+3000 - U+303F)
        if (cp >= 0x3000 && cp <= 0x303F) return true;
        // 全角标点 (U+FF00 - U+FFEF)
        if (cp >= 0xFF00 && cp <= 0xFFEF) return true;
        // 通用标点 (U+2000 - U+206F)
        if (cp >= 0x2000 && cp <= 0x206F) return true;

        return false;
    }
}// namespace TextUtils
