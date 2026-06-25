#pragma once
#include <muduo/net/TcpConnection.h>
#include <muduo/base/Logging.h>
#include <string>

struct Message
{
    uint8_t type;      // 1: 关键字推荐 2: 网页搜索
    uint32_t length;   // 数据长度（网络字节序）
    std::string value; // 实际数据
};

// 长度前缀编解码器：4字节长度 + 1字节type + 数据
class MessageCodec : public muduo::noncopyable
{
public:
    using StringMessageCallback = std::function<void(
        const muduo::net::TcpConnectionPtr &, const Message &, muduo::Timestamp)>;

    explicit MessageCodec(const StringMessageCallback &cb) : callback_(cb) {}

    void onMessage(const muduo::net::TcpConnectionPtr &conn,
                   muduo::net::Buffer *buf, muduo::Timestamp receiveTime)
    {
        // 循环解析，可能一次收到多个包
        while (buf->readableBytes() >= kHeaderLen)
        {
            // 读取类型 (1字节) 和长度 (4字节大端)
            uint8_t type = static_cast<uint8_t>(*(buf->peek()));
            uint32_t be_len = 0;
            memcpy(&be_len, buf->peek() + 1, sizeof(be_len));
            uint32_t len = muduo::net::sockets::networkToHost32(be_len);

            if (buf->readableBytes() >= kHeaderLen + len)
            {
                buf->retrieve(kHeaderLen); // 消费头部
                std::string data = buf->retrieveAsString(len);
                Message msg{type, len, std::move(data)};
                callback_(conn, msg, receiveTime);
            }
            else
            {
                break; // 数据不全，等待下一次
            }
        }
    }

    // 发送消息
    static void send(const muduo::net::TcpConnectionPtr &conn, const Message &msg)
    {
        muduo::net::Buffer buf;

        buf.append(&msg.type, sizeof(msg.type));
        uint32_t be_len = muduo::net::sockets::hostToNetwork32(msg.length);
        buf.append(&be_len, sizeof(be_len));
        buf.append(msg.value);
        conn->send(&buf);
    }

private:
    static constexpr size_t kHeaderLen = sizeof(uint8_t) + sizeof(uint32_t); // 5
    StringMessageCallback callback_;
};