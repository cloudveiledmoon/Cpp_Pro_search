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
         try {
        json j = json::parse(msg.value);
        if (msg.type == 1) {
            // 处理网页搜索结果（JSON 数组）
            if (!j.is_array()) {
                std::cerr << "Invalid response format for type 1" << std::endl;
            } else {
                std::cout << "Search results:" << std::endl;
                int count = 1;
                for (const auto& item : j) {
                    // 提取需要的字段（根据实际返回的 JSON 结构调整）
                    std::string id = item.value("id", "?");
                    std::string title = item.value("title", "No title");
                    std::string abstract = item.value("abstract", "");
                    
                    std::cout << count++ << ". [" << id << "] " << title << std::endl;
                    if (!abstract.empty()) {
                        std::cout << "   " << abstract << std::endl;
                    }
                }
            }
        } else if (msg.type == 2) {
            // 处理关键词推荐（原逻辑）
            if (j.contains("words") && j["words"].is_array()) {
                auto words = j["words"];
                std::cout << "Recommended words: ";
                for (const auto& w : words) {
                    std::cout << w.get<std::string>() << " ";
                }
                std::cout << std::endl;
            }
        } else {
            std::cout << "Unknown message type: " << (int)msg.type << std::endl;
        }
    } catch (const json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error processing message: " << e.what() << std::endl;
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
                    req.type = 2;
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