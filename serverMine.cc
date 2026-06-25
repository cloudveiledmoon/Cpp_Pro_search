#include "serverMine.h"
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <utfcpp/utf8.h>
#include <muduo/base/Logging.h>
#include "KeywordProcessor.h"

using namespace muduo;
using namespace muduo::net;
using json = nlohmann::json;
using namespace std;

int ServerMine::minDistance(const string &word1, const string &word2)
{
    int n = word1.length();
    int m = word2.length();

    // 有一个字符串为空串
    if (n * m == 0)
        return n + m;

    // DP 数组
    vector<vector<int>> D(n + 1, vector<int>(m + 1));

    // 边界状态初始化
    for (int i = 0; i < n + 1; i++)
    {
        D[i][0] = i;
    }
    for (int j = 0; j < m + 1; j++)
    {
        D[0][j] = j;
    }

    // 计算所有 DP 值
    for (int i = 1; i < n + 1; i++)
    {
        for (int j = 1; j < m + 1; j++)
        {
            int left = D[i - 1][j] + 1;
            int down = D[i][j - 1] + 1;
            int left_down = D[i - 1][j - 1];
            if (word1[i - 1] != word2[j - 1])
                left_down += 1;
            D[i][j] = min(left, min(down, left_down));
        }
    }
    return D[n][m];
}

//  生成关键词推荐
json ServerMine::makeSummary(const std::string &query)
{

    // 解析查询中的中文字符，统计相关词频
    std::map<std::string, int> word_count;
    const char *character = query.c_str();
    const char *end = query.c_str() + query.size();
    while (character != end)
    {
        auto temp = character;
        utf8::next(character, end);
        std::string addc(temp, character);
        if (KeywordProcessor::is_chinese_token(addc))
        {
            auto it = index_.find(addc);
            if (it != index_.end())
            {
                for (int wd_id : it->second)
                {
                    word_count[dict_[wd_id]]++;
                }
            }
        }
    }
    map<std::string, int> word_distence;
    for (auto &pair : word_count)
    {
        word_distence[pair.first] = minDistance(pair.first, query);
    }
    // 按词频降序排序，取前5个
    std::vector<std::pair<std::string, int>> sorted(word_count.begin(), word_count.end());
    std::sort(sorted.begin(), sorted.end(), [word_distence](const auto &a, const auto &b)
              { 
        int a_dist = word_distence.find(a.first)->second;
        int b_dist = word_distence.find(b.first)->second;
                if(a_dist == b_dist){
                    return a.second > b.second;
                }
                return a_dist < b_dist; });

    json words_array = json::array();
    for (int i = 0; i < std::min(5, (int)sorted.size()); ++i)
    {
        words_array.push_back(sorted[i].first);
    }

    // 不足5个时用默认词填充
    while (words_array.size() < 5)
    {
        words_array.push_back("NONE");
    }
    return json{{"words", words_array}};
}


json ServerMine::makeWebpage(const std::string &query){
    vector<string> query_word;
    tokenizer_.Cut(query,query_word);
    map<string,int> query_map;
    for(auto word:query_word){
        if(KeywordProcessor::is_chinese_token(word)&&stopwords_.find(word) == stopwords_.end()){
            query_map[word]++;
        }
    }
    map<string,double> tf_doc;
    for(auto word:query_map){
        tf_doc[word.first] = (static_cast<double>(word.second)/query_map.size()+0.0);
    }
    map<string,double> idf_tf_doc;
    for(auto word:query_map){
        double N = static_cast<double>(invertedIndex_.size()+1.0);
        double df = static_cast<double>(invertedIndex_[word.first].size()+1.0);
        idf_tf_doc[word.first] = log2(N/(df+1.0))*tf_doc[word.first];
    }

    vector<int> 

 


    


}

std::vector<int> ServerMine::searchAllKeywords(const std::map<std::string, int>& query_map){
    vector<int> result;
    bool flag = true;
    for(auto &word:query_map){
        if(invertedIndex_.find(word.first) == invertedIndex_.end()){
                return {};
            }
        if(flag){
            for(auto doc:invertedIndex_[word.first]){
                result.push_back(doc.first);
            }
            flag = false;
            continue;
        }
        for(auto &id:result){
            if(invertedIndex_[word.first].find(id) == invertedIndex_[word.first].end()){
                result.erase(std::remove(result.begin(),result.end(),id),result.end());
            }
        }
        if(result.empty()){
            return {};
        }
    }
    return result;
}
// ---------- 处理请求 ----------
void ServerMine::onRequest(const TcpConnectionPtr &conn,
                           const Message &msg,
                           Timestamp)
{
    json summary;
    if (msg.type == 1)
    { // 查询请求
         summary = makeSummary(msg.value);
        
    }else if(msg.type == 2){
        summary = makeWebpage(msg.value);
    }
    std::string respBody = summary.dump();

        Message respMsg;
        respMsg.type = 2;
        respMsg.length = respBody.size();
        respMsg.value = respBody;
        MessageCodec::send(conn, respMsg);
}

ServerMine::ServerMine(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const std::string &name)
    : server_(loop, listenAddr, name),
      codec_(std::bind(&ServerMine::onRequest, this, _1, _2, _3))
{
    server_.setConnectionCallback([](const TcpConnectionPtr &conn)
                                  {
        if (conn->connected()) LOG_INFO << "New connection"; });
    server_.setMessageCallback(
        [this](const TcpConnectionPtr &conn, Buffer *buf, Timestamp t)
        {
            codec_.onMessage(conn, buf, t);
        });
    server_.setThreadNum(4);
    std::ifstream dict_in("./data/dict/cn.dict");
    std::ifstream index_in("./data/index/cn.index");
    std::string line;
    build_stopword();
    build_inverse_index();
    // 读词典（格式：词 词频）
    int id = 0;
    while (getline(dict_in, line))
    {
        if (line.empty())
            continue;
        std::istringstream iss(line);
        std::string word;
        iss >> word;
        if (!word.empty())
        {
            dict_[++id] = word;
        }
    }

    // 读倒排索引（格式：字 文档ID列表...）
    while (getline(index_in, line))
    {
        if (line.empty())
            continue;
        std::istringstream iss(line);
        std::string word;
        iss >> word;
        int doc_id;
        while (iss >> doc_id)
        {
            index_[word].insert(doc_id);
        }
    }
    

    dict_in.close();
    index_in.close();
}
void ServerMine::build_stopword(){
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

void ServerMine::build_inverse_index(){
    std::ifstream index_in("./data/index/web.index");
    std::ifstream doc_in("./data/dict/web.offset");
    std::string line;
    while(doc_in){
        int id;
        int offset;
        int size;
        doc_in >> id >> offset >> size;
        doc_[id]={offset,size};
    }
    
    std::string line;
    while (std::getline(index_in, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string word;
        iss >> word; // 第一个词
        int docId;
        double weight;
        std::map<int, double> docWeights;
        while (iss >> docId >> weight) {
            docWeights[docId] = weight;
        }
        invertedIndex_[word] = std::move(docWeights);
    }


    index_in.close();
    doc_in.close();
}


void ServerMine::start()
{
    server_.start();
}