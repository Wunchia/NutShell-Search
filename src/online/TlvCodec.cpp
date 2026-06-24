#include "online/TlvCodec.h"

#include <cstdint>
#include <muduo/net/Buffer.h>
#include <muduo/net/Endian.h>
#include <cstring>

void TlvCodec::onMessage(const muduo::net::TcpConnectionPtr& conn,
                         muduo::net::Buffer* buf,Timestamp time)
{
    while(buf->readableBytes()>=kHeaderLen){
        //peek头部
        uint8_t type=0;
        uint32_t length=0;
        memcpy(&type, buf->peek(), 1);
        memcpy(&length, buf->peek()+1, kHeaderLen-1);

        //网络序->主机序
        length=muduo::net::sockets::networkToHost32(length);

        if(buf->readableBytes()<kHeaderLen+length){
            break;//数据没到齐，等下次TCP再发数据
        }

        //数据到齐
        //取出头部
        buf->retrieve(kHeaderLen);

        //读value
        Message msg;
        msg.type=type;
        msg.value.assign(buf->peek(),length);
        //清空缓冲区
        buf->retrieve(length);

        //回调业务层
        _callback(conn,msg,time);
    }
}

void TlvCodec::send(const muduo::net::TcpConnectionPtr& conn,
                    const Message& message)
{
    muduo::net::Buffer buf;

    buf.append(&message.type,1);

    //多字节整数需要转为网络字节序
    uint32_t lenBE=muduo::net::sockets::hostToNetwork32(
        static_cast<uint32_t>(message.value.size())
    );
    buf.append(&lenBE,4);

    buf.append(message.value.data(),message.value.size());

    conn->send(&buf);
}
