#pragma once
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpServer.h>
#include <nlohmann/json.hpp>
#include "MessageCodec.h"
#include<set>
#include<map>
#include<string>
#include<cppjieba/Jieba.hpp>
#include<simhash/Simhasher.hpp>
#include <sys/types.h>
using json = nlohmann::json;
class ServerMine {
public:
    // 构造函数：传入事件循环、监听地址和服务器名称
    ServerMine(muduo::net::EventLoop* loop,
                  const muduo::net::InetAddress& listenAddr,
                  const std::string& name = "ServerMine");

    // 启动服务器
    void start();
    void build_stopword();
    void build_inverse_index();
    std::vector<int> searchAllKeywords(const std::map<std::string, int>& query_map,const std::map<std::string,double>& idf_tf_doc);
    json parseDocFragment(const std::string& xmlContent,const std::string & first_keyword);
    std::string getAbstract(const std::string &abstract,const std::string &first_keyword);
private:
    int minDistance(const std::string& word1, const std::string& word2);
    // 处理客户端请求的回调
    void onRequest(const muduo::net::TcpConnectionPtr& conn,
                   const Message& msg,
                   muduo::Timestamp receiveTime);

    // 根据查询生成摘要（返回5个词的JSON）
    nlohmann::json makeSummary(const std::string& query);
    nlohmann::json makeWebpage(const std::string &query);
    muduo::net::TcpServer server_;
    MessageCodec codec_;   // 消息编解码器
    std::map<int, std::string> dict_;
    std::map<std::string, std::set<int>> index_;
    private:
    struct Document
    {
        int id;
        std::string title;
        std::string link;
        std::string content;
    };
    cppjieba::Jieba tokenizer_;
    simhash::Simhasher hasher_;
    std::map<int,std::pair<int,off_t>> doc_;
    std::set<std::string> stopwords_;
    std::map<std::string,std::map<int,double>> invertedIndex_;


};