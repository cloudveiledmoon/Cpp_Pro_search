#pragma once
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include<nlohmann/json.hpp>

struct Documents
    {
        int id;
        std::string title;
        std::string link;
        std::string content;
    };
class HashCaches
{
public:
   HashCaches(){
      word_capacity = 30;web_capacity = 30;
   }
    HashCaches(int word_capacity,int web_capacity);
    std::vector<std::string> get_word(const std::string &key);
    void put_word(const std::string &key, const std::vector<std::string> &value);
    nlohmann::json get_web(const int &id,const std::string &first_keyword);
    void put_web(const int &key, const Documents &value);
public:
    
private:
    
    struct Words
    {
        std::string key;
        std::vector<std::string> words;
        int time;
        int count;
        Words(std::string key,std::vector<std::string> word)
        :key(key),words(word),time(0),count(0){

        }

    }; 
    struct Web
    {
        int key;
        Documents documents;
        int time;
        int count;
        Web(int key,Documents doc,int time)
        :key(key),documents(doc),time(time),count(0){

        }
    };
    
private:
    int web_capacity;
    int word_capacity;
    int time_;
    std::map<int,std::vector<Words>> word_caches_;
    std::map<int,std::vector<Web>> web_caches_;
    template<typename T>
    int hash(T& key);

};
