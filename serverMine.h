#pragma once
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpServer.h>
#include <nlohmann/json.hpp>
#include "MessageCodec.h"

class ServerMine {
public:
    // 构造函数：传入事件循环、监听地址和服务器名称
    ServerMine(muduo::net::EventLoop* loop,
                  const muduo::net::InetAddress& listenAddr,
                  const std::string& name = "ServerMine");

    // 启动服务器
    void start();

private:
    // 处理客户端请求的回调
    void onRequest(const muduo::net::TcpConnectionPtr& conn,
                   const Message& msg,
                   muduo::Timestamp receiveTime);

    // 根据查询生成摘要（返回5个词的JSON）
    nlohmann::json makeSummary(const std::string& query) const;

    muduo::net::TcpServer server_;
    MessageCodec codec_;   // 消息编解码器
};