#include <iostream>
#include <string>
#include "serverMine.h"
#include "cacheService.h"
using namespace std;
using json = nlohmann::json;
HashCaches::HashCaches(int word_capacity, int web_capacity)
    : word_capacity(word_capacity), web_capacity(web_capacity)
{
    word_mutexes_ = std::vector<std::mutex>(8);
    web_mutexes_ = std::vector<std::mutex>(8);
}
std::vector<std::string> HashCaches::get_word(const std::string &key)
{
    int pos = hash(key);
    
    std::lock_guard<std::mutex> lock(word_mutexes_[pos]); // 锁住对应桶
    if (word_capacity == 0 || word_caches_[pos].size() == 0)
    {
        return {};
    }
    for (auto wr : word_caches_[pos])
    {
        if (wr.key == key)
        {
            return wr.words;
            wr.count++;
            wr.time = time_ + 1;
        }
    }
    return {};
}
void HashCaches::put_word(const std::string &key, const std::vector<std::string> &value)
{
    int pos = hash(key);
     std::lock_guard<std::mutex> lock(word_mutexes_[pos]); // 锁住对应桶
    if (word_capacity == word_caches_[pos].size())
    {
        auto elemt = word_caches_[pos].begin();
        for (auto wd = word_caches_[pos].begin(); wd != word_caches_[pos].end(); wd++)
        {
            if (wd->count < elemt->count)
            {
                elemt = wd;
            }
            else if (wd->count == elemt->count)
            {
                if (wd->time < elemt->time)
                {
                    elemt = wd;
                }
            }
        }

        word_caches_[pos].erase(elemt);
        word_caches_[pos].push_back(Words(key, value));

    }
    else
    {
        word_caches_[pos].push_back(Words(key, value));
    }
}
json HashCaches::get_web(const int &id, const std::string &first_keyword)
{
    int pos=hash(id);
     std::lock_guard<std::mutex> lock(web_mutexes_[pos]); // 锁住对应桶
    json ret = json::object();
    if (web_capacity == 0 || web_caches_[pos].size() == 0)
    {
        return {};
    }
    for (auto wr : web_caches_[pos])
    {
        
        if (wr.key == id)
        {
            wr.time = time_ + 1;
            wr.count++;
            Documents retdoc = wr.documents;
            ret["id"] = id;
            ret["title"] = retdoc.title;
            ret["link"] = retdoc.link;
            ret["abstract"] = ServerMine::getAbstract(retdoc.content, first_keyword);
            return ret;
        }
    }
    return {};
}
void HashCaches::put_web(const int &key, const Documents &value)
{
    int pos=hash(key);
     std::lock_guard<std::mutex> lock(web_mutexes_[pos]); // 锁住对应桶
    if (web_capacity == web_caches_[pos].size())
    {
        auto target = web_caches_[pos].begin();
        for (auto it = web_caches_[pos].begin(); it != web_caches_[pos].end(); ++it)
        {
            if (it->count < target->count || (it->count == target->count && it->time < target->time))
            {
                target = it;
            }
        }
        web_caches_[pos].erase(target);
        web_caches_[pos].push_back(Web(key, value,++time_));
    }
    else
    {
        web_caches_[pos].push_back(Web(key, value,++time_));
    }
}
template<>
int HashCaches::hash(std::string& key)
{   
   size_t h = std::hash<std::string>{}(key);
        // 返回模 8 的结果（与你的 int 版本保持一致）
        return static_cast<int>(h % 8);
}
template<>
int HashCaches::hash(int& key)
{
    return key%8;
}