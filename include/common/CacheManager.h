#pragma once

#include <hiredis/hiredis.h>
#include <string>

// ============================================================
//  CacheManager — 两级缓存 (L1 进程内 LRU + L2 Redis)
// ============================================================
// 查询链路: L1 → L2 → 计算 → 回写 L1+L2
// Redis 不可用时自动降级为纯 L1

class CacheManager
{
public:
    CacheManager();
    ~CacheManager();

    //初始化Redis连接
    bool initRedis(const std::string& host,int port);

    // ==========关键字推荐缓存========
    bool getKeyword(const std::string& query,std::string& result);
    void putKeyword(const std::string& query,const std::string& result);

    // ==========网页搜索缓存==========
    bool getSearch(const std::string& query,std::string& result);
    void putSearch(const std::string& query,const std::string& result);

private:
    //Redis 读写
    bool redisSet(const std::string& key,const std::string& value,int ttl);
    bool redisGet(const std::string& key,std::string& value);

    //拼缓存 key
    static std::string kwKey(const std::string& q){return "ns:kw:"+q;}
    static std::string searchKey(const std::string& q){return "ns:search:"+q;}

    redisContext* _redis=nullptr;//Redis 连接（nullptr 表示未启用）

public:
    //L1 进程内缓存容量
    static constexpr size_t KW_CAPACITY =5000;
    static constexpr size_t SEARCH_CAPACITY =3000;
    static constexpr int KW_TTL=3600; //关键字 1小时
    static constexpr int SEARCH_TTL=1800;//网页搜索30分钟
};
