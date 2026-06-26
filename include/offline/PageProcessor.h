#pragma once

#include <simhash/Simhasher.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

class PageProcessor
{
public:
    PageProcessor();
    void process(const std::string& dir);

private:
    void extract_documents(const std::string& dir);
    void deduplicate_documents();
    void build_pages_and_offsets(const std::string& pages,
                                 const std::string& offsets);
    void build_inverted_index(const std::string& filename);

private:
    struct Document{
        int id=0;
        std::string link;
        std::string title;
        std::string content;
    };

private:
    simhash::Simhasher _hasher;
    std::set<std::string> _stopWords;
    std::vector<Document> _documents;
    std::map<std::string, std::map<int,double>> _invertedIndex;
};
