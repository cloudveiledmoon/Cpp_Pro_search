#pragma once
#include<cppjieba/Jieba.hpp>
#include<string>
#include<set>
class KeywordProcessor
{
public:

    KeywordProcessor();
    void process(const std::string& chDir,const std::string& enDir);

private:
    void create_cn_dict(const std::string& dir,const std::string& outfile);
    void build_cn_index(const std::string& dict,const std::string& index);
    void create_en_dict(const std::string& dir,const std::string& outfile);
    void build_en_index(const std::string& dict,const std::string& index);
    bool is_chinese_token(const std::string& token);
private:
    cppjieba::Jieba tokenizer_;
    std::set<std::string> cn_set_;
    std::set<std::string> en_set_;

};

