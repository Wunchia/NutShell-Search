#pragma once

#include <set>
#include <string>

// 加载停用词文件 每行一个词
std::set<std::string> load_stop_words(const std::string& filename);
