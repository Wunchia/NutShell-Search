#include "online/KeywordService.h"
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

    //加载关键字推荐的离线数据
    KeywordService keywordService;
    keywordService.load("../data/index/dict_en.dat",
                        "../data/index/index_en.dat",
                        "../data/index/dict_cn.dat",
                        "../data/index/index_cn.dat");

    //TLV编解码器
    TlvCodec codec([&keywordService](
                    const muduo::net::TcpConnectionPtr& conn,
                    const Message& msg,Timestamp time){
        //根据type分发 1=关键字推荐, 2=网页搜索
        if(msg.type==1){
            std::string result=keywordService.query(msg.value);
            std::cout<<"[keyword] querry: "<<msg.value
                     <<" -> "<<result<<std::endl;
        }else{
            std::cout<<"[PageSearch] query: "<<msg.value
                     <<" todo ... "<<std::endl;
        }
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
