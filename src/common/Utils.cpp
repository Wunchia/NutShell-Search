#include "common/Utils.h"

#include <fstream>
#include <set>
#include <stdexcept>
#include <string>

std::set<std::string> load_stop_words(const std::string &filename)
{
    std::set<std::string> words;
    std::ifstream ifs(filename);
    if(!ifs){
        throw std::runtime_error("Cannot open stopwords file:"+filename);
    }
    std::string word;
    while(ifs>>word){
        words.insert(word);
    }
    return words;
}
