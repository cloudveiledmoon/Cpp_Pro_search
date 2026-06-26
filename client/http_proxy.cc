// http_proxy.cc
#include <iostream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <functional>
#include <fstream> 
#include <cstring>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/Buffer.h>
#include <muduo/base/Logging.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include "../MessageCodec.h"   // 你的自定义协议编解码器

using namespace muduo;
using namespace muduo::net;
using json = nlohmann::json;

// ========== 后端连接配置 ==========
static const std::string BACKEND_IP = "127.0.0.1";
static const uint16_t    BACKEND_PORT = 2947;
static const size_t      HEADER_SIZE = sizeof(uint8_t) + sizeof(uint32_t); // 5

// ========== 工具函数：同步发送请求并接收响应 ==========
// 返回后端响应的 value 字段（JSON 字符串），失败返回空串
static std::string sendRequestToBackend(uint8_t type, const std::string& query) {
    // 1. 创建 socket 并连接
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return "";

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(BACKEND_PORT);
    if (inet_pton(AF_INET, BACKEND_IP.c_str(), &serverAddr.sin_addr) <= 0) {
        close(sockfd);
        return "";
    }

    if (connect(sockfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sockfd);
        return "";
    }

    // 2. 打包请求消息
    Message req;
    req.type = type;
    req.value = query;
    req.length = req.value.size();

    muduo::net::Buffer sendBuf;
    // 写入 type (1字节)
    sendBuf.append(&req.type, sizeof(req.type));
    // 写入 length (4字节大端)
    uint32_t be_len = sockets::hostToNetwork32(static_cast<uint32_t>(req.length));
    sendBuf.append(reinterpret_cast<const char*>(&be_len), sizeof(be_len));
    // 写入数据
    sendBuf.append(req.value);

    // 3. 发送请求
    ssize_t n = ::send(sockfd, sendBuf.peek(), sendBuf.readableBytes(), 0);
    if (n != static_cast<ssize_t>(sendBuf.readableBytes())) {
        close(sockfd);
        return "";
    }

    // 4. 接收响应头
    uint8_t respType;
    uint32_t respLenNet;
    n = ::recv(sockfd, &respType, sizeof(respType), MSG_WAITALL);
    if (n != sizeof(respType)) { close(sockfd); return ""; }
    n = ::recv(sockfd, &respLenNet, sizeof(respLenNet), MSG_WAITALL);
    if (n != sizeof(respLenNet)) { close(sockfd); return ""; }
    uint32_t respLen = sockets::networkToHost32(respLenNet);

    // 5. 接收响应体
    std::string respValue(respLen, '\0');
    n = ::recv(sockfd, &respValue[0], respLen, MSG_WAITALL);
    if (n != static_cast<ssize_t>(respLen)) { close(sockfd); return ""; }

    close(sockfd);
    return respValue;   // JSON 字符串
}

// ========== 简单的 HTTP 请求解析器 ==========
class SimpleHttpParser {
public:
    enum State { kRequestLine, kHeaders, kBody, kDone };

    SimpleHttpParser() : state_(kRequestLine), contentLength_(0) {}

    // 尝试从 Buffer 中解析 HTTP 请求，返回 true 表示解析完成
    bool parse(Buffer* buf) {
        while (buf->readableBytes() > 0) {
            if (state_ == kRequestLine) {
                const char* crlf = buf->findCRLF();
                if (crlf) {
                    std::string line = buf->retrieveAsString(crlf - buf->peek());
                    buf->retrieve(2);  // 跳过 \r\n
                    std::istringstream iss(line);
                    iss >> method_ >> path_;
                    state_ = kHeaders;
                } else {
                    return false; // 数据不足
                }
            } else if (state_ == kHeaders) {
                const char* crlf = buf->findCRLF();
                if (crlf) {
                    std::string line = buf->retrieveAsString(crlf - buf->peek());
                    buf->retrieve(2);
                    if (line.empty()) { // 头部结束
                        if (contentLength_ > 0) {
                            state_ = kBody;
                        } else {
                            state_ = kDone;
                            return true; // 无 body
                        }
                    } else {
                        // 解析 Content-Length
                        size_t colon = line.find(':');
                        if (colon != std::string::npos) {
                            std::string key = line.substr(0, colon);
                            std::string value = line.substr(colon + 1);
                            // 去除前导空格
                            size_t start = value.find_first_not_of(" \t");
                            if (start != std::string::npos)
                                value = value.substr(start);
                            // 关键头部不区分大小写
                            if (strcasecmp(key.c_str(), "Content-Length") == 0) {
                                contentLength_ = std::stoi(value);
                            }
                        }
                    }
                } else {
                    return false;
                }
            } else if (state_ == kBody) {
                if (buf->readableBytes() >= contentLength_) {
                    body_ = buf->retrieveAsString(contentLength_);
                    state_ = kDone;
                    return true;
                } else {
                    return false; // 数据不足
                }
            } else {
                break;
            }
        }
        return false;
    }

    std::string method() const { return method_; }
    std::string path() const { return path_; }
    std::string body() const { return body_; }

private:
    State state_;
    size_t contentLength_;
    std::string method_;
    std::string path_;
    std::string body_;
};

// ========== 代理服务器 ==========
class HttpProxyServer {
public:
    HttpProxyServer(EventLoop* loop, const InetAddress& listenAddr)
        : server_(loop, listenAddr, "HttpProxy") {
        server_.setConnectionCallback(
            std::bind(&HttpProxyServer::onConnection, this, _1));
        server_.setMessageCallback(
            std::bind(&HttpProxyServer::onMessage, this, _1, _2, _3));
    }

    void start() { server_.start(); }

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            // 为每个连接分配一个解析器
            conn->setContext(std::make_shared<SimpleHttpParser>());
        }
    }

    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp) {
        auto parser = boost::any_cast<std::shared_ptr<SimpleHttpParser>>(conn->getContext());
        if (!parser) return;

        if (!parser->parse(buf)) {
            return; // 数据未收全，等待
        }

        const std::string& method = parser->method();
        const std::string& path = parser->path();
        const std::string& body = parser->body();
           // ====== 新增：处理静态文件请求 ======
    if (method == "GET") {
        std::string filePath = "." + path;                     // 例如 /index.html → ./index.html
        // 简单防止路径遍历（禁止 ..）
        if (filePath.find("..") != std::string::npos) {
            std::string resp = "HTTP/1.1 403 Forbidden\r\n"
                               "Content-Length: 9\r\n\r\nForbidden";
            conn->send(resp);
            conn->shutdown();
            return;
        }
        // 如果请求根路径，默认返回 index.html
        if (path == "/") filePath = "./index.html";

        std::ifstream file(filePath, std::ios::binary);
        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            size_t len = file.tellg();
            file.seekg(0, std::ios::beg);
            std::string content(len, '\0');
            file.read(&content[0], len);
            file.close();

            std::ostringstream oss;
            oss << "HTTP/1.1 200 OK\r\n"
                << "Content-Type: text/html; charset=utf-8\r\n"
                << "Content-Length: " << len << "\r\n"
                << "Connection: close\r\n\r\n"
                << content;
            conn->send(oss.str());
        } else {
            std::string notFound = "HTTP/1.1 404 Not Found\r\n"
                                   "Content-Length: 9\r\n\r\nNot Found";
            conn->send(notFound);
        }
        conn->shutdown();
        return;
    }
        // 处理 CORS 预检请求
        if (method == "OPTIONS") {
            std::string response = "HTTP/1.1 200 OK\r\n"
                                   "Access-Control-Allow-Origin: *\r\n"
                                   "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
                                   "Access-Control-Allow-Headers: Content-Type\r\n"
                                   "Content-Length: 0\r\n\r\n";
            conn->send(response);
            conn->shutdown();
            return;
        }

        // 只处理 POST /query
        if (method != "POST" || path != "/query") {
            std::string response = "HTTP/1.1 404 Not Found\r\n"
                                   "Access-Control-Allow-Origin: *\r\n"
                                   "Content-Type: text/plain\r\n"
                                   "Content-Length: 9\r\n"
                                   "Connection: close\r\n\r\n"
                                   "Not Found";
            conn->send(response);
            conn->shutdown();
            return;
        }

        // 解析 JSON 请求体
        json reqJson;
        try {
            reqJson = json::parse(body);
        } catch (...) {
            std::string response = "HTTP/1.1 400 Bad Request\r\n"
                                   "Access-Control-Allow-Origin: *\r\n"
                                   "Content-Type: text/plain\r\n"
                                   "Content-Length: 20\r\n"
                                   "Connection: close\r\n\r\n"
                                   "Invalid JSON";
            conn->send(response);
            conn->shutdown();
            return;
        }

        uint8_t type = reqJson.value("type", 0);
        std::string query = reqJson.value("query", "");
        if (type == 0 || query.empty()) {
            std::string response = "HTTP/1.1 400 Bad Request\r\n"
                                   "Access-Control-Allow-Origin: *\r\n"
                                   "Content-Type: text/plain\r\n"
                                   "Content-Length: 20\r\n"
                                   "Connection: close\r\n\r\n"
                                   "Missing type/query";
            conn->send(response);
            conn->shutdown();
            return;
        }

        // 调用后端服务
        std::string backendResp = sendRequestToBackend(type, query);
        if (backendResp.empty()) {
            std::string response = "HTTP/1.1 502 Bad Gateway\r\n"
                                   "Access-Control-Allow-Origin: *\r\n"
                                   "Content-Type: text/plain\r\n"
                                   "Content-Length: 19\r\n"
                                   "Connection: close\r\n\r\n"
                                   "Backend unavailable";
            conn->send(response);
            conn->shutdown();
            return;
        }

        // 构造成功响应
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Content-Type: application/json; charset=utf-8\r\n"
            << "Content-Length: " << backendResp.size() << "\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << backendResp;
        conn->send(oss.str());
        conn->shutdown();   // 短连接
    }

    TcpServer server_;
};

// ========== main ==========
int main(int argc, char* argv[]) {
    uint16_t proxyPort = 8080;
    if (argc > 1) proxyPort = static_cast<uint16_t>(atoi(argv[1]));

    muduo::Logger::setLogLevel(muduo::Logger::WARN);   // 减少日志输出
    EventLoop loop;
    InetAddress listenAddr(proxyPort);
    HttpProxyServer proxy(&loop, listenAddr);

    proxy.start();
    std::cout << "HTTP proxy started on port " << proxyPort
              << ", forwarding to " << BACKEND_IP << ":" << BACKEND_PORT << std::endl;
    loop.loop();
    return 0;
}