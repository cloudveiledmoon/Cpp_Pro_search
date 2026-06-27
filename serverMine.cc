#include "serverMine.h"
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <vector>
#include <utility>
#include <algorithm>
#include <utfcpp/utf8.h>
#include <muduo/base/Logging.h>
#include "KeywordProcessor.h"
#include <tinyxml2.h>

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
    // 本地缓存获取
    std::vector<string> cache = cacheService_.get_word(query);
    if (!cache.empty())
    {
        json cache_json = json::array();
        for (auto &str : cache)
        {
            cache_json.push_back(str);
        }
        cerr << "cache hit" << std::endl;
        return cache_json;
    }

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
    // 本地缓存存储
    std::vector<std::string> return_sorted;
    for (auto &item : sorted)
    {
        return_sorted.push_back(item.first);
    }
    cacheService_.put_word(query, return_sorted);

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

json ServerMine::makeWebpage(const std::string &query)
{

    vector<string> query_word;
    tokenizer_.Cut(query, query_word);
    map<string, int> query_map;
    string frist_keyword = query_word[0];
    for (auto word : query_word)
    {
        if (KeywordProcessor::is_chinese_token(word) && stopwords_.find(word) == stopwords_.end())
        {
            query_map[word]++;
        }
    }
    map<string, double> tf_doc;
    for (auto word : query_map)
    {
        tf_doc[word.first] = (static_cast<double>(word.second) / query_map.size() + 0.0);
    }

    map<string, double> idf_tf_doc; // idf
    for (auto word : query_map)
    {
        double N = static_cast<double>(invertedIndex_.size() + 1.0);
        double df = static_cast<double>(invertedIndex_[word.first].size() + 1.0);
        idf_tf_doc[word.first] = log2(N / (df + 1.0)) * tf_doc[word.first];
    }

    std::vector<int> result = searchAllKeywords(query_map, idf_tf_doc);
    json web_array = json::array();
    if (result.empty())
    {
        return web_array;
    }

    // string frist_keyword = std::max_element(query_map.begin(), query_map.end(),
    //                                         [](const auto &a, const auto &b)
    //                                         {
    //                                             return a.second < b.second;
    //                                         })
    //                            ->first;

    ifstream ifs("./data/dict/web.xml");
    if (!ifs.is_open())
    {
        return web_array;
    }

    int id_count = 12;
    for (auto &id : result)
    {
        if (id_count-- == 0)
        {
            break;
        }

        // 本地缓存获取
        json cache = cacheService_.get_web(id, frist_keyword);
        if (!cache.empty())
        {
            web_array.push_back(cache);
            continue;
        }

        auto offset = doc_[id].first;
        auto size = doc_[id].second;

        ifs.seekg(offset, std::ios::beg);
        string xmlbuf = string(size, ' ');
        ifs.read(&xmlbuf[0], size);

        // 检查读取到的片段是否以 <doc 开头，若不是则尝试前后微调（不推荐长期方案）
        if (xmlbuf.find("<doc") != 0)
        {
            // 可能偏移错误，跳过或报错
            cerr << "Skipping doc " << id << " due to invalid fragment start" << endl;
            continue;
        }
        // 确保末尾是 </doc>
        if (xmlbuf.rfind("</doc>") == string::npos)
        {
            cerr << "Fragment for doc " << id << " does not end with </doc>" << endl;
            continue;
        }
        json doc = parseDocFragment(xmlbuf, frist_keyword);
        web_array.push_back(doc);
        std::string title = doc.value("title", "");
        std::string link = doc.value("link", "");
        std::string content = doc.value("content", "");
        Documents value = {id, title, link, content};
        cacheService_.put_web(id, value);
    }
    return web_array;
}
json ServerMine::parseDocFragment(const std::string &xmlContent, const std::string &first_keyword)
{
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError err = doc.Parse(xmlContent.c_str());
    if (err != tinyxml2::XML_SUCCESS)
    {
        throw std::runtime_error("Failed to parse XML fragment");
    }

    tinyxml2::XMLElement *docElem = doc.FirstChildElement("doc");
    if (!docElem)
    {
        throw std::runtime_error("No <doc> element found");
    }

    json result = json::object();
    if (auto *elem = docElem->FirstChildElement("id"))
        result["id"] = elem->GetText() ? elem->GetText() : "";
    if (auto *elem = docElem->FirstChildElement("title"))
        result["title"] = elem->GetText() ? elem->GetText() : "";
    if (auto *elem = docElem->FirstChildElement("link"))
        result["link"] = elem->GetText() ? elem->GetText() : "";
    if (auto *elem = docElem->FirstChildElement("content"))
    {
        string abstract = elem->GetText() ? elem->GetText() : "";
        result["abstract"] = getAbstract(abstract, first_keyword);
    }

    return result;
}

string ServerMine::getAbstract(const string &abstract, const string &first_keyword)
{
    if (abstract.empty())
    {
        return "";
    }

    size_t pos = abstract.find(first_keyword);
    if (pos == std::string::npos)
    {
        // 关键词不存在，返回全文的前若干字符或空串，根据业务决定
        const char *safe_begin = abstract.data();
        const char *safe_end = abstract.data() + abstract.size();
        const char *safe_pos = safe_begin;
        for (int i = 0; i < 50; i++)
        {
            utf8::next(safe_pos, safe_end);
        }
        return std::string(safe_begin, safe_pos - safe_begin) + "...";
    }
    const char *safe_begin = abstract.data();
    const char *safe_end = abstract.data() + abstract.size();
    const char *safe_pos = safe_begin;

    size_t char_count = 0;
    while (safe_pos < safe_begin + pos)
    {
        utf8::next(safe_pos, safe_end);
        char_count++;
    }

    const size_t preChars = 15;
    const size_t postChars = 35;
    size_t startChar = (char_count >= preChars) ? (char_count - preChars) : 0;
    size_t endChar = utf8::distance(first_keyword.c_str(), first_keyword.c_str() + first_keyword.size());
    size_t totalChars = preChars + endChar + postChars;

    const char *startPtr = safe_begin;
    for (size_t i = 0; i < startChar; i++)
    {
        utf8::next(startPtr, safe_end);
    }
    const char *endPtr = startPtr;
    for (size_t i = 0; i < totalChars && endPtr < safe_end; i++)
    {
        utf8::next(endPtr, safe_end);
    }
    std::string snippet = std::string(startPtr, endPtr - startPtr);
    if (endPtr < safe_end)
    {
        snippet += "...";
    }
    return snippet;
}
std::vector<int> ServerMine::searchAllKeywords(const std::map<std::string, int> &query_map, const std::map<std::string, double> &idf_tf_doc)
{
    map<int, map<string, double>> result;
    bool flag = true;
    for (auto &word : query_map)
    {
        if (invertedIndex_.find(word.first) == invertedIndex_.end())
        {
            return {};
        }
        if (flag)
        {
            for (auto doc : invertedIndex_[word.first])
            {
                result[doc.first][word.first] = doc.second;
            }
            flag = false;
            continue;
        }

        for (auto it = result.begin(); it != result.end();)
        {
            int docId = it->first;     // 文档 ID
            auto &docVec = it->second; // 该文档的词权重 map

            // 检查该文档是否包含当前词
            auto pos = invertedIndex_[word.first].find(docId);
            if (pos == invertedIndex_[word.first].end())
            {
                // 不包含，删除该文档
                it = result.erase(it); // erase 返回下一个有效迭代器
            }
            else
            {
                // 包含，记录该词在该文档中的权重
                docVec[word.first] = pos->second; // 或直接使用 invertedIndex_[word.first][docId]
                ++it;
            }
        }

        if (result.empty())
        {
            return {};
        }
    }
    double moduleX = 0;
    for (auto &needX : idf_tf_doc)
    {
        moduleX += needX.second * needX.second;
    }
    moduleX = sqrt(moduleX); // X模长

    map<int, double> cosine;
    for (auto &id : result)
    {
        double mod = 0;
        for (auto &needY : id.second)
        {
            mod += needY.second * needY.second;
        }
        mod = sqrt(mod);
        double XY = 0;
        for (auto &xx : idf_tf_doc)
        {
            XY += xx.second * id.second[xx.first];
        }
        cosine[id.first] = XY / (moduleX * mod);
    }

    std::vector<std::pair<int, double>> items(cosine.begin(), cosine.end());

    // 按余弦值降序排序
    std::sort(items.begin(), items.end(),
              [](const auto &a, const auto &b)
              { return a.second > b.second; });
    std::vector<int> webResult;
    webResult.reserve(items.size());
    for (const auto &[docId, sim] : items)
    {
        webResult.push_back(docId);
    }
    return webResult;
}

// ---------- 处理请求 ----------
void ServerMine::onRequest(const muduo::net::TcpConnectionPtr &conn,
                           const Message &msg,
                           muduo::Timestamp receiveTime)
{
    json summary;
    if (msg.type == 1)
    { // 查询请求
        summary = makeSummary(msg.value);
    }
    else if (msg.type == 2)
    {
        summary = makeWebpage(msg.value);
    }
    std::string respBody = summary.dump();

    Message respMsg;
    respMsg.type = 1;
    respMsg.length = respBody.size();
    respMsg.value = respBody;

    MessageCodec::send(conn, respMsg);
}

ServerMine::ServerMine(EventLoop *loop, const InetAddress &listenAddr, const std::string &name)
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
void ServerMine::build_stopword()
{
    ifstream cn_in("./stopwords/cn_stopwords.txt");
    ifstream en_in("./stopwords/en_stopwords.txt");

    while (cn_in)
    {
        std::string word;
        cn_in >> word;
        stopwords_.insert(word);
    }
    while (en_in)
    {
        std::string word;
        en_in >> word;
        stopwords_.insert(word);
    }
    en_in.close();
    cn_in.close();
}

void ServerMine::build_inverse_index()
{
    std::ifstream index_in("./data/index/web.index");
    std::ifstream doc_in("./data/dict/web.offset");
    std::string line;
    int id;
    uint64_t offset; // 必须 64 位
    size_t size;     // size_t 通常 64 位
    while (doc_in >> id >> offset >> size)
    {
        doc_[id] = {static_cast<std::streamoff>(offset), size};
    }

    while (std::getline(index_in, line))
    {
        if (line.empty())
            continue;
        std::istringstream iss(line);
        std::string word;
        iss >> word; // 第一个词
        int docId;
        double weight;
        std::map<int, double> docWeights;
        while (iss >> docId >> weight)
        {
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