#pragma once

#include <string>
#include <vector>

namespace TextUtils{
    // 判断字符是否为 ASCII 字母(a-z,A-Z)
    bool is_ascii_alpha(char ch);
    // 英文行规范化：去标点数字、转小写
    std::string normalize_english_line(const std::string& line);
    // 将UTF-8词语拆分为单个字符
    std::vector<std::string> split_utf8_characters(const std::string& word);
    // 判断 token 是否为中文标点或空白字符
    bool is_chinese_punctuation_or_space(const std::string& token);
}
