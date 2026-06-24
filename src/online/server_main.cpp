#include "online/TlvCodec.h"

#include <muduo/base/Timestamp.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpServer.h>

#include <iostream>

int main()
{
    //事件循环
    muduo::net::EventLoop loop;
    //监听地址
    muduo::net::InetAddress addr(8848);
    //TCP服务器
    muduo::net::TcpServer server(&loop,addr,"NutShellSearch");
    //TLV编解码器
    TlvCodec codec([](const muduo::net::TcpConnectionPtr& conn,
                      const Message& msg,Timestamp time){
        std::cout<<"msg type: "<<static_cast<int>(msg.type)
                 <<" msg value: "<<msg.value<<std::endl;
    });
    //注册链接回调
    server.setConnectionCallback(
        [](const muduo::net::TcpConnectionPtr& conn){
            if(conn->connected()){
                std::cout<<"New connection from "
                         <<conn->peerAddress().toIpPort()<<std::endl;
            }else{
                std::cout<<"Connection closed"<<std::endl;
            }
        }
    );
    //注册消息回调 TCP数据 -> TlvCodec解码
    server.setMessageCallback(
        [&codec](const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buf,Timestamp time){
            codec.onMessage(conn, buf,time);
        });
    //启动
    server.start();
    std::cout<<"NutShellSearch server listening on port 8848"<<std::endl;
    loop.loop();

    return 0;
}
