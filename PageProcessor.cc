#include <iostream>
#include<my_header.h>
#include "PageProcessor.h"
#include<tinyxml2.h>
#include<algorithm>
#include<bitset>
using namespace tinyxml2;
using namespace simhash;
using namespace std;
PageProcessor::PageProcessor(){
    ifstream cn_in("./stopwords/cn_stopwords.txt");
    ifstream en_in("./stopwords/en_stopwords.txt");

    while(cn_in){
        std::string word;
        cn_in >> word;
        stopwords_.insert(word);
    }
    while(en_in){
        std::string word;
        en_in >> word;
        stopwords_.insert(word);
    }
    en_in.close();
    cn_in.close();
}
void PageProcessor::process(const std::string &dir){
    extract_documents(dir);
    
    build_simhash();
    build_pages_and_offsets("./data/dict/web.xml","./data/dict/web.offset");

    build_inevrted_index("./data/index/web.index");
}

bool PageProcessor::is_chinese_token(const std::string& token) {
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

void PageProcessor::build_pages_and_offsets(const std::string &pagename,const std::string &offsets){
    
    off_t offset=0;
    ofstream page_on(pagename);
    ofstream offset_on(offsets);
    
    for(auto &doc:documents_){
        XMLDocument page;
        XMLElement *doct=page.NewElement("doc");
        page.InsertEndChild(doct);
        XMLElement *id=page.NewElement("id");
        id->SetText(doc.id);
        doct->InsertEndChild(id);
        XMLElement *title=page.NewElement("title");
        title->SetText(doc.title.c_str());
        doct->InsertEndChild(title);
        XMLElement *link=page.NewElement("link");
        link->SetText(doc.link.c_str());
        doct->InsertEndChild(link);
        XMLElement *content=page.NewElement("content");
        content->SetText(doc.content.c_str());
        doct->InsertEndChild(content);
        
        XMLPrinter printer; 
        page.Print(&printer);
        std::string page_str=printer.CStr();
        page_on<<page_str;
        offset_on<<std::to_string(doc.id)+" "+std::to_string(offset)+" "+std::to_string(page_str.size())<<endl;
        offset+=page_str.size();
        
    }
    page_on.close();
    offset_on.close();
}

void PageProcessor::build_inevrted_index(const std::string &filename){
    std::ofstream save_on(filename);
    map<string,int> dict;
    map<int,map<string,int>> book;
    map<string, set<int>> word_docs;   // 词 → 出现的文档ID集合
    
        for(auto & doc:documents_){
            vector<string> tokens_word;
            tokenizer_.Cut(doc.content,tokens_word);

            for(auto& token:tokens_word){
                if(token.size()>1&&!stopwords_.count(token)&&is_chinese_token(token)){
                    book[doc.id][token]++;
                    dict[token]++;
                    word_docs[token].insert(doc.id);
                   
                }
            }
        }

        map<int,map<string,double>> tf_dic;//每个文档的词频
        for(auto& doc:book){ 
            for(auto & item:doc.second){
                tf_dic[doc.first][item.first]=static_cast<double>(item.second)/doc.second.size();
            }
        }
        
        map<string,int> doc_freq;//每个词出现的文档数

        for(auto &wd:word_docs){
            doc_freq[wd.first]=wd.second.size();
        }   

        map<string,double> idf_dic;//每个词的idf
        for(auto &fdoc:dict){
            double N = static_cast<double>(book.size());
            double df = static_cast<double>(doc_freq[fdoc.first]);
            idf_dic[fdoc.first]=log2(N/(df+1.0));
        }
        
       
        for(auto &doc:book){ 
            map<string,double> tf_idf;//tf-idf关键字权重
            double sqr_sum=0;
            for(auto &keyword:doc.second){
                tf_idf[keyword.first]=tf_dic[doc.first][keyword.first]*idf_dic[keyword.first];
                sqr_sum+=pow(tf_idf[keyword.first],2);
            }

            for(auto &ftd:tf_idf){
                invertedIndex_[ftd.first][doc.first]=ftd.second/sqrt(sqr_sum);
            }
        }

        for(auto &file:invertedIndex_){
            save_on<<file.first<<" ";
            for(auto &doc:file.second){
                save_on<<doc.first<<" "<<doc.second<<" ";
            }
            save_on<<std::endl;
        }
        
        save_on.close();
}

int PageProcessor::hamming_distance(const uint64_t &a, const uint64_t &b){
    int distance = 0;
    uint64_t t=a^b;
    while(t){ 
        t &= t-1;
        distance++;
    }
    return distance;
}
void PageProcessor::build_simhash(){
    set<uint64_t> hamming_set;
    vector<Document> temp;
    int id=0;
    for(auto doc=documents_.begin();doc!=documents_.end();++doc){
        uint64_t doc_hash;
        auto topN=max(5,min(200,(int)(doc->content.size())/120));
        hasher_.make(doc->content,topN,doc_hash);
        bool flag = true;
        for(auto &hash:hamming_set){
            if(Simhasher::isEqual(hash,doc_hash)){
                flag = false;
                break;
            }
        }
        if(flag){
            hamming_set.insert(doc_hash);
            temp.push_back({++id,doc->title,doc->link,doc->content});
        }
    }
   documents_ = temp; 
}
void PageProcessor::xml_to_documents(const std::string &filename,int &id){
    XMLDocument doc;
    XMLError err = doc.LoadFile(filename.c_str());
    if(err != XML_SUCCESS){
        cerr<<"Error: "<<filename<<" "<<err<<endl;
        return;
    }
    XMLElement *rss = doc.FirstChildElement("rss");
    if(!rss){cerr<<"Error: "<<filename<<" "<<"no rss"<<endl;return;}
    XMLElement *channel = rss->FirstChildElement("channel");
    if(!channel){cerr<<"Error: "<<filename<<" "<<"no channel"<<endl;return;}
    for(XMLElement *item = channel->FirstChildElement("item");item!=nullptr;item = item->NextSiblingElement("item")){
         XMLElement *title = item->FirstChildElement("title");
         if(!title){cerr<<"Error: "<<filename<<" "<<"no title"<<endl;continue;}
         XMLElement *link = item->FirstChildElement("link");
         if(!link){cerr<<"Error: "<<filename<<" "<<"no link"<<endl;continue;}
         XMLElement *content = item->FirstChildElement("content");
         if(!content){
            content = item->FirstChildElement("description");
            if(!content){
                //cerr<<"Error: "<<filename<<" "<<"no content"<<endl;
                continue;
            }
        }
        documents_.push_back({++id,title->GetText(),link->GetText(),content->GetText()});
    }
}
void PageProcessor::extract_documents(const std::string &dir){
    vector<string> files = DirectoryScanner::scan(dir);
    int id=0;
    for(auto file:files){
        string filepath=dir+"/"+file;
        xml_to_documents(filepath,id);
    }      
}