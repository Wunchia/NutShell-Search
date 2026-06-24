#pragma once

#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/TcpConnection.h>
#include <cstdint>
#include <functional>
#include <string>

using muduo::Timestamp;

struct Message{
    uint8_t type; //消息的类型 1=关键字推荐 2=网页搜索
    //uint32_t length; //value的长度 作为编解码时的临时数据，不做持久化的业务数据存储
    std::string value; //消息的内容
};

class TlvCodec
{
public:
    //回调
    using MessageCallback=std::function<void(
        const muduo::net::TcpConnectionPtr&conn,
        const Message&msg,
        Timestamp time)>;
    //构造
    explicit TlvCodec(MessageCallback cb)
        :_callback(std::move(cb)) {}

    //muduo框架在每次收到Tcp数据时调用
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,Timestamp time);
    //编码发送 业务层调用
    void send(const muduo::net::TcpConnectionPtr& conn,
              const Message& message);

private:
    //头部长度：type(1)+length(4)=5
    static const size_t kHeaderLen=5;

    //消息回调，构造时注入，由 onMessage 在每次收到完整消息时调用
    MessageCallback _callback;
};
