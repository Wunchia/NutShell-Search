#include "online/KeywordService.h"
#include "online/WebSearchService.h"
#include "online/TlvCodec.h"

#include <muduo/base/Timestamp.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/http/HttpServer.h>
#include <muduo/net/http/HttpRequest.h>
#include <muduo/net/http/HttpResponse.h>

#include <fstream>
#include <iostream>
#include <sstream>

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

    //加载网页搜索的离线数据
    WebSearchService webSearchService;
    webSearchService.load("../data/index/invert_index.dat",
        "../data/index/pages.dat",
        "../data/index/offsets.dat");

    //TLV编解码器
    TlvCodec codec([&](
        const muduo::net::TcpConnectionPtr& conn,
        const Message& msg,Timestamp){
            //根据type分发 1=关键字推荐, 2=网页搜索
            if(msg.type==1){
                std::string result=keywordService.query(msg.value);
                Message response;
                response.type=1;
                response.value=result;
                codec.send(conn,response);
                std::cout<<"[keyword] querry: "<<msg.value
                    <<" -> "<<result<<std::endl;
            }else if(msg.type==2){
                std::string result=webSearchService.query(msg.value);
                Message response;
                response.type=2;
                response.value=result;
                codec.send(conn,response);
                std::cout<<"[PageSearch] querry: "<<msg.value
                    <<" -> "<<result.size()<<" bytes"<<std::endl;
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

    // ==========================================
    //  HTTP 服务 (8080) — 前端 + REST API
    // ==========================================
    // 预读前端页面到内存（启动时读一次，之后所有请求直接返回）
    std::string indexHtml;
    {
        std::ifstream ifs("../static/index.html");
        if (ifs) {
            std::ostringstream oss;
            oss << ifs.rdbuf();
            indexHtml = oss.str();
            std::cout << "[HTTP] Loaded index.html: "
                << indexHtml.size() << " bytes" << std::endl;
        } else {
            std::cerr << "[HTTP] WARNING: ../static/index.html not found!" << std::endl;
        }
    }

    muduo::net::InetAddress httpAddr(8080);
    muduo::net::HttpServer httpServer(&loop, httpAddr, "NutShellHttp");

    httpServer.setHttpCallback([&](const muduo::net::HttpRequest& req,
        muduo::net::HttpResponse* resp) {
            // CORS: 允许前端跨域（前后端同域时可省略，加上无副作用）
            resp->addHeader("Access-Control-Allow-Origin", "*");
            std::string basePath = req.path();
            std::string queryStr = req.query();

            // 从 query string 中提取 q= 参数
            auto getQueryParam = [&](const std::string& key) -> std::string {
                std::string prefix = key + "=";
                size_t start = queryStr.find(prefix);
                if (start == std::string::npos) return "";
                start += prefix.size();
                size_t end = queryStr.find('&', start);
                std::string val = (end == std::string::npos)
                    ? queryStr.substr(start)
                    : queryStr.substr(start, end - start);

                // URL 解码
                std::string decoded;
                for (size_t i = 0; i < val.size(); ++i) {
                    if (val[i] == '%' && i + 2 < val.size()) {
                        int hi = std::stoi(val.substr(i + 1, 2), nullptr, 16);
                        decoded += static_cast<char>(hi);
                        i += 2;
                    } else if (val[i] == '+') {
                        decoded += ' ';
                    } else {
                        decoded += val[i];
                    }
                }
                return decoded;
            };

            // ------------------------------------------------------
            // 路由 ①：GET /  → 返回前端页面
            // ------------------------------------------------------
            if (basePath == "/" || basePath == "/index.html") {
                resp->setStatusCode(muduo::net::HttpResponse::k200Ok);
                resp->setContentType("text/html; charset=utf-8");
                resp->setBody(indexHtml);
                return;
            }

            // ------------------------------------------------------
            // 路由 ②：GET /api/keyword?q=xxx → 关键字推荐 JSON
            // ------------------------------------------------------
            if (basePath == "/api/keyword") {
                std::string q = getQueryParam("q");
                std::string json;
                if (!q.empty()) {
                    json = keywordService.query(q, 8);
                } else {
                    json = "[]";
                }
                resp->setStatusCode(muduo::net::HttpResponse::k200Ok);
                resp->setContentType("application/json; charset=utf-8");
                resp->setBody(json);
                std::cout << "[HTTP] keyword?q=" << q << std::endl;
                return;
            }

            // ------------------------------------------------------
            // 路由 ③：GET /api/search?q=xxx → 网页搜索 JSON
            // ------------------------------------------------------
            if (basePath == "/api/search") {
                std::string q = getQueryParam("q");
                std::string json;
                if (!q.empty()) {
                    json = webSearchService.query(q, 10);
                } else {
                    json = "[]";
                }
                resp->setStatusCode(muduo::net::HttpResponse::k200Ok);
                resp->setContentType("application/json; charset=utf-8");
                resp->setBody(json);
                std::cout << "[HTTP] search?q=" << q << std::endl;
                return;
            }

            // 静态资源：背景图片
            if (basePath == "/background.png") {
                std::ifstream ifs("../static/background.png", std::ios::binary);
                if (ifs) {
                    std::string img((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
                    resp->setStatusCode(muduo::net::HttpResponse::k200Ok);
                    resp->setContentType("image/png");
                    resp->setBody(img);
                } else {
                    resp->setStatusCode(muduo::net::HttpResponse::k404NotFound);
                }
                return;
            }

            // ------------------------------------------------------
            // 兜底：404
            // ------------------------------------------------------
            resp->setStatusCode(muduo::net::HttpResponse::k404NotFound);
            resp->setContentType("text/plain");
            resp->setBody("Not Found");
        });

    httpServer.start();
    std::cout << "NutShellSearch HTTP server listening on port 8080" << std::endl;
    // ==========================================
    //  HTTP 服务 (8080) — 前端 + REST API
    // ==========================================

    loop.loop();

    return 0;
}
