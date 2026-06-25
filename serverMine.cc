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

// ---------- 生成摘要 ----------
json ServerMine::makeSummary(const std::string& query) const {
    std::ifstream dict_in("./data/dict/cn.dict");
    std::ifstream index_in("./data/index/cn.index");
    std::map<int, std::string> dict_;
    std::map<std::string, std::set<int>> index_;
    std::string line;

    // 读词典（格式：词 词频）
    int id = 0;
    while (getline(dict_in, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string word;
        iss >> word;
        if (!word.empty()) {
            dict_[id] = word;
            ++id;
        }
    }

    // 读倒排索引（格式：字 文档ID列表...）
    while (getline(index_in, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string word;
        iss >> word;
        int doc_id;
        while (iss >> doc_id) {
            index_[word].insert(doc_id);
        }
    }

    // 解析查询中的中文字符，统计相关词频
    std::map<std::string, int> word_count;
    const char* character = query.c_str();
    const char* end = query.c_str() + query.size();
    while (character != end) {
        auto temp = character;
        utf8::next(character, end);
        std::string addc(temp, character);
        if (KeywordProcessor::is_chinese_token(addc)) {
            auto it = index_.find(addc);
            if (it != index_.end()) {
                for (int wd_id : it->second) {
                    auto dit = dict_.find(wd_id);
                    if (dit != dict_.end()) {
                        word_count[dit->second]++;
                    }
                }
            }
        }
    }

    // 按词频降序排序，取前5个
    std::vector<std::pair<std::string, int>> sorted(word_count.begin(), word_count.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    json words_array = json::array();
    for (int i = 0; i < std::min(5, (int)sorted.size()); ++i) {
        words_array.push_back(sorted[i].first);
    }

    // 不足5个时用默认词填充
    while (words_array.size() < 5) {
        words_array.push_back("nothing");
    }

    return json{{"words", words_array}};
}

// ---------- 处理请求 ----------
void ServerMine::onRequest(const TcpConnectionPtr& conn,
                              const Message& msg,
                              Timestamp) {
    if (msg.type == 1) {   // 查询请求
        json summary = makeSummary(msg.value);
        std::string respBody = summary.dump();

        Message respMsg;
        respMsg.type = 2;
        respMsg.length = respBody.size();
        respMsg.value = respBody;
        MessageCodec::send(conn, respMsg);
    }
}

// ---------- 构造函数 ----------
ServerMine::ServerMine(EventLoop* loop,
                             const InetAddress& listenAddr,
                             const std::string& name)
    : server_(loop, listenAddr, name),
      codec_(std::bind(&ServerMine::onRequest, this, _1, _2, _3))
{
    server_.setConnectionCallback([](const TcpConnectionPtr& conn) {
        if (conn->connected()) LOG_INFO << "New connection";
    });
    server_.setMessageCallback(
        [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp t) {
            codec_.onMessage(conn, buf, t);
        });
    server_.setThreadNum(4);
}

// ---------- 启动服务器 ----------
void ServerMine::start() {
    server_.start();
}