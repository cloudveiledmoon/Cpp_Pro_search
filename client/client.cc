#include "../MessageCodec.h"
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpClient.h>
#include <muduo/base/Logging.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <atomic>

using namespace muduo;
using namespace muduo::net;
using json = nlohmann::json;

class QueryClient {
public:
    QueryClient(EventLoop* loop, const InetAddress& serverAddr)
        : client_(loop, serverAddr, "QueryClient"),
          loop_(loop) {
        MessageCodec::StringMessageCallback cb =
            [this](const TcpConnectionPtr& conn, const Message& msg, Timestamp) {
                onResponse(conn, msg);
            };
        codec_ = std::make_unique<MessageCodec>(cb);
        client_.setMessageCallback(
            [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp t) {
                codec_->onMessage(conn, buf, t);
            });
        client_.setConnectionCallback(
            [this](const TcpConnectionPtr& conn) { onConnection(conn); });
    }

    void connect() {
        client_.connect();
        // 启动输入线程
        inputThread_ = std::thread([this] { inputLoop(); });
    }

    void disconnect() {
        running_ = false;
        if (inputThread_.joinable()) {
            inputThread_.join();
        }
    }

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            LOG_WARN << "Connected to server";
            connection_ = conn;   // 保存连接
        } else {
            LOG_WARN << "Disconnected";
            connection_.reset();
            loop_->quit();        // 断开后退出事件循环
        }
    }

    void onResponse(const TcpConnectionPtr&, const Message& msg) {
        if (msg.type == 2) {
            json j = json::parse(msg.value);
            auto words = j["words"];
            std::cout << "Received words: ";
            for (auto& w : words) std::cout << w.get<std::string>() << " ";
            std::cout << std::endl;
            std::cout<<"Enter query: "<<std::flush;
        }
    }
    void inputLoop() {
        std::string query;
        while (running_) {
            std::cout << "Enter query: ";
            if (!std::getline(std::cin, query)) break;
            if (query.empty()) continue;
            // 将发送任务投递到网络线程执行
            loop_->queueInLoop([this, query] {
                if (connection_) {
                    Message req;
                    req.type = 1;
                    req.length = query.size();
                    req.value = query;
                    MessageCodec::send(connection_, req);
                }
            });
        }
    }

    TcpClient client_;
    std::unique_ptr<MessageCodec> codec_;
    EventLoop* loop_;
    TcpConnectionPtr connection_;
    std::thread inputThread_;
    std::atomic<bool> running_{true};
};

int main() {
    //muduo::Logger::setLogLevel(muduo::Logger::ERROR);   // 显示日志
    EventLoop loop;
    InetAddress addr("127.0.0.1", 2947);
    QueryClient client(&loop, addr);
    client.connect();
    loop.loop();
    client.disconnect();
    return 0;
}