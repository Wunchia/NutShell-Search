#include "online/KeywordService.h"
#include "online/WebSearchService.h"
#include "online/TlvCodec.h"
#include "common/CacheManager.h"
#include "common/Config.h"

#include <muduo/base/Timestamp.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/http/HttpServer.h>
#include <muduo/net/http/HttpRequest.h>
#include <muduo/net/http/HttpResponse.h>
#include <muduo/base/Logging.h>

#include <fstream>
#include <iostream>
#include <sstream>

int main()
{
    // 加载配置文件
    Config::instance().load("../conf/config.conf");
    auto& cfg = Config::instance();

    // 从配置读取日志级别
    std::string logLevel = cfg.get("LOG_LEVEL", "INFO");
    if (logLevel == "DEBUG")      muduo::Logger::setLogLevel(muduo::Logger::DEBUG);
    else if (logLevel == "WARN")  muduo::Logger::setLogLevel(muduo::Logger::WARN);
    else if (logLevel == "ERROR") muduo::Logger::setLogLevel(muduo::Logger::ERROR);
    else                           muduo::Logger::setLogLevel(muduo::Logger::INFO);

    //事件循环
    muduo::net::EventLoop loop;
    //监听地址
    muduo::net::InetAddress addr(8848);
    //TCP服务器
    muduo::net::TcpServer server(&loop,addr,"NutShellSearch");

    //加载关键字推荐的离线数据
    KeywordService keywordService;
    keywordService.load(
        cfg.get("KEYWORD_DICT_EN_PATH",  "../data/index/dict_en.dat"),
        cfg.get("KEYWORD_INDEX_EN_PATH", "../data/index/index_en.dat"),
        cfg.get("KEYWORD_DICT_CN_PATH",  "../data/index/dict_cn.dat"),
        cfg.get("KEYWORD_INDEX_CN_PATH", "../data/index/index_cn.dat"));

    //加载网页搜索的离线数据
    WebSearchService webSearchService;
    webSearchService.load(
        cfg.get("WEBPAGE_INVERT_PATH",  "../data/index/invert_index.dat"),
        cfg.get("WEBPAGE_PAGELIB_PATH", "../data/index/pages.dat"),
        cfg.get("WEBPAGE_OFFSET_PATH",  "../data/index/offsets.dat"));

    //初始化缓存（Redis 连不上就纯 L1运行）
    CacheManager cache;
    cache.initRedis(
        cfg.get("REDIS_HOST", "127.0.0.1"),
        cfg.getInt("REDIS_PORT", 6379));

    //TLV编解码器
    TlvCodec codec([&](
        const muduo::net::TcpConnectionPtr& conn,
        const Message& msg,Timestamp){
            //根据type分发 1=关键字推荐, 2=网页搜索
            if(msg.type==1){
                std::string result;
                if(!cache.getKeyword(msg.value,result)){
                    result=keywordService.query(msg.value);
                    cache.putKeyword(msg.value,result);
                }
                Message response;
                response.type=1;
                response.value=result;
                codec.send(conn,response);
                LOG_INFO<<"[keyword]"<<msg.value;
            }else if(msg.type==2){
                std::string result;
                if(!cache.getSearch(msg.value,result)){
                    result=webSearchService.query(msg.value);
                    cache.putSearch(msg.value, result);
                }
                Message response;
                response.type=2;
                response.value=result;
                codec.send(conn,response);
                LOG_INFO<<"[PageSearch]"<<msg.value<<" -> "<<result.size()<<" bytes";
            }
        });
    //注册链接回调
    server.setConnectionCallback(
        [](const muduo::net::TcpConnectionPtr& conn){
            if(conn->connected()){
                LOG_INFO<<"New connection from "<<conn->peerAddress().toIpPort();
            }else{
                LOG_INFO<<"Connection closed";
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
    LOG_INFO<<"TLV server listening on port 8848";

    // ==========================================
    //  HTTP 服务 (8080) — 前端 + REST API
    // ==========================================
    // 预读前端页面到内存（启动时读一次，之后所有请求直接返回）
    std::string indexHtml;
    {
        std::ifstream ifs(cfg.get("STATIC_DIR", "../static") + "/index.html");
        if (ifs) {
            std::ostringstream oss;
            oss << ifs.rdbuf();
            indexHtml = oss.str();
            LOG_INFO<<"Loaded index.html: "<<indexHtml.size()<<" bytes";
        } else {
            LOG_ERROR<<"index.html not found!";
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
                    if(!cache.getKeyword(q,json)){
                    json = keywordService.query(q, 8);
                    cache.putKeyword(q, json);
                    }
                } else {
                    json = "[]";
                }
                resp->setStatusCode(muduo::net::HttpResponse::k200Ok);
                resp->setContentType("application/json; charset=utf-8");
                resp->setBody(json);
                LOG_INFO<<"[HTTP] keyword?q="<<q;
                return;
            }

            // ------------------------------------------------------
            // 路由 ③：GET /api/search?q=xxx → 网页搜索 JSON
            // ------------------------------------------------------
            if (basePath == "/api/search") {
                std::string q = getQueryParam("q");
                std::string json;
                if (!q.empty()) {
                    if(!cache.getSearch(q, json)){
                    json = webSearchService.query(q, 10);
                    cache.putSearch(q, json);
                    }
                } else {
                    json = "[]";
                }
                resp->setStatusCode(muduo::net::HttpResponse::k200Ok);
                resp->setContentType("application/json; charset=utf-8");
                resp->setBody(json);
                LOG_INFO<<"[HTTP] search?q="<<q;
                return;
            }

            // 静态资源：背景图片
            if (basePath == "/background.png") {
                std::ifstream ifs(cfg.get("STATIC_DIR", "../static") + "/background.png", std::ios::binary);
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
    LOG_INFO<<"HTTP Server listening on port 8080";

    loop.loop();

    return 0;
}
