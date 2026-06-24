#pragma once
#include<string>
#include"DirectoryScanner.h"
#include<set>
#include<vector>
#include<cppjieba/Jieba.hpp>
#include<simhash/Simhasher.hpp>

class PageProcessor
{
public:
    PageProcessor();
    void process(const std::string &dir);

private:
    void build_pages_and_offsets(const std::string &pagename,const std::string &offsets);

    void build_inevrted_index(const std::string &filename);

    void build_simhash();
    bool is_chinese_token(const std::string& token);
    int hamming_distance(const uint64_t &a, const uint64_t &b);

    void xml_to_documents(const std::string &filename,int &id);

    void extract_documents(const std::string &dir);
private:
    struct Document
    {
        int id;
        std::string title;
        std::string link;
        std::string content;
    };
private:
    cppjieba::Jieba tokenizer_;
    simhash::Simhasher hasher_;
    std::vector<Document> documents_;
    std::set<std::string> stopwords_;
    std::map<std::string,std::map<int,double>> invertedIndex_;
};

