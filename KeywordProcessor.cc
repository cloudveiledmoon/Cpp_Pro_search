#include <iostream>
#include <fstream>
#include<utfcpp/utf8.h>
#include "KeywordProcessor.h"
#include "DirectoryScanner.h"
#include<cctype>
using namespace std;
bool KeywordProcessor::is_chinese_token(const std::string& token) {
    for (size_t i = 0; i < token.size(); ) {
        unsigned char c = token[i];
        if (c <= 0x7F) {
            // ASCII 字符：如果是数字或标点，直接过滤
            if (isdigit(c) || ispunct(c)) return false;
            i++;
        } else {
            // 多字节 UTF-8，简单判断是否在中日韩统一表意文字区间 (U+4E00 ~ U+9FFF)
            uint32_t cp = 0;
            int len = 0;
            if ((c & 0xE0) == 0xC0) { len = 2; cp = (c & 0x1F) << 6; }
            else if ((c & 0xF0) == 0xE0) { len = 3; cp = (c & 0x0F) << 12; }
            else if ((c & 0xF8) == 0xF0) { len = 4; cp = (c & 0x07) << 18; }
            else { i++; continue; }
            if (len > 1) {
                for (int j = 1; j < len; ++j) {
                    cp |= (token[i+j] & 0x3F) << (6 * (len - j - 1));
                }
            }
            if (cp >= 0x4E00 && cp <= 0x9FFF) return true;  // 发现中文字符
            i += len;
        }
    }
    return false;  // 没有中文字符，过滤
}
KeywordProcessor::KeywordProcessor(){
    ifstream cn_in("./stopwords/cn_stopwords.txt");
    ifstream en_in("./stopwords/en_stopwords.txt");

    while(cn_in){
        std::string word;
        cn_in >> word;
        cn_set_.insert(word);
    }
    while(en_in){
        std::string word;
        en_in >> word;
        en_set_.insert(word);
    }
    en_in.close();
    cn_in.close();
}
void KeywordProcessor::process(const std::string& chDir,const std::string& enDir){

    create_cn_dict(chDir, "./data/dict/cn.dict");
    create_en_dict(enDir, "./data/dict/en.dict");
    build_cn_index("./data/dict/cn.dict", "./data/index/cn.index");
    build_en_index("./data/dict/en.dict", "./data/index/en.index");
}

void KeywordProcessor::create_cn_dict(const std::string& dir,const std::string& outfile){
    std::vector<std::string> files = DirectoryScanner::scan(dir);
    std::map<std::string, int> dict;
    //vector<string> tokens;
    for(auto& file:files){
        std::ifstream in(dir+"/"+file);
        std::string line; 
        while(std::getline(in, line)){
            vector<string> tokens_word;
            tokenizer_.Cut(line,tokens_word);
            for(auto& token:tokens_word){
                if(token.size()>1&&!cn_set_.count(token)&&is_chinese_token(token)){
                    //tokens.push_back(token);
                    dict[token]++;
                }
            }
        }
        in.close();
    }
    ofstream out(outfile);
    for(auto& item:dict){
        out<<item.first<<" "<<item.second<<endl;
    }
    out.close();
}
void KeywordProcessor::build_cn_index(const std::string& dict,const std::string& index){
    ifstream in(dict);
    if(!in){
        cout<<"Cannot open "<<dict<<endl;
        return;
    }
    std::map<string,set<int>> indexs;
    string line;
    int linenum=0;
    while(getline(in,line)){ 
        linenum++;
        
        const char *character =line.c_str();
        const char *end = line.c_str() + line.size();
        while(character != end){
           auto temp=character;
            utf8::next(character,end);
            string addc=string(temp,character);
            if(is_chinese_token(addc)){
                indexs[addc].insert(linenum);
            }
            
        }  
    }
    in.close();
    ofstream ofile(index);
    if(!ofile.is_open())
       {
             return;
        }

    for(auto &ide:indexs){
        ofile<<ide.first<<" ";
        for(auto &j:ide.second){
            ofile<<j<<" ";
        }
        ofile<<"\n";
    }
    ofile.close();
}
void KeywordProcessor::create_en_dict(const std::string& dir,const std::string& outfile){
    std::vector<std::string> files = DirectoryScanner::scan(dir);
    if(files.size()==0){
        return;
    }
    std::map<std::string,int> dict;

    for(auto &file:files){
        ifstream in(dir+"/"+file);
        string word;
        while(in>>word){
            string clean_word;
            for(auto &c:word){
                if(isalpha(c)){
                    clean_word+= tolower(c);
                }else{
                    if(!clean_word.empty()&&en_set_.find(clean_word)==en_set_.end()){
                        dict[clean_word]++;
                    }
                    clean_word.clear();
                }
            }
            
            if(!clean_word.empty()&&en_set_.find(clean_word)==en_set_.end()){
                dict[clean_word]++;
            }
        }
    }
    ofstream out(outfile);
    for(auto &pair:dict){
        out<<pair.first<<" "<<pair.second<<endl;
    }
    out.close();
}
void KeywordProcessor::build_en_index(const std::string& dict,const std::string& index){
ifstream in(dict);
    if(!in){
        cout<<"Cannot open "<<dict<<endl;
        return;
    }
    std::map<char,set<int>> indexs;
    string line;
    int linenum=0;
    while(getline(in,line)){ 
        linenum++;
        string word=line.substr(0,line.find(" "));
        for(int i=0;i<word.size();i++){
            indexs[word[i]].insert(linenum);
        }
    }
    in.close();
    ofstream ofile(index);
    if(!ofile.is_open())
       {
             return;
        }

    for(auto &ide:indexs){
        ofile<<ide.first<<" ";
        for(auto &j:ide.second){
            ofile<<j<<" ";
        }
        ofile<<"\n";
    }
    ofile.close();
}

