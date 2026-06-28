#include "common/LRUCache.h"
#include "common/CacheManager.h"

#include <hiredis/hiredis.h>
#include <hiredis/read.h>
#include <muduo/base/Logging.h>
#include <iostream>

namespace {
    ShardedLRUCache<std::string, std::string> _kwCache(CacheManager::KW_CAPACITY);
    ShardedLRUCache<std::string, std::string> _searchCache(CacheManager::SEARCH_CAPACITY);
}

CacheManager::CacheManager()=default;
CacheManager::~CacheManager(){
    if(_redis){redisFree(_redis);}
}

// ====================================
// Redis 连接
// ====================================
bool CacheManager::initRedis(const std::string& host,int port)
{
    _redis=redisConnect(host.c_str(), port);
    if(_redis==nullptr||_redis->err){
        LOG_WARN<<"Redis connect failed, running L1-only";
        if(_redis){redisFree(_redis);_redis=nullptr;}
        return false;
    }
    LOG_INFO<<"Redis connected to "<<host<<":"<<port;
    return true;
}


// ====================================
// Redis 读写
// ====================================
bool CacheManager::redisSet(const std::string& key,const std::string& value,int ttl){
    if(!_redis){return false;}
    auto* reply=static_cast<redisReply*>(
        redisCommand(_redis, "SETEX %s %d %b", key.c_str(),ttl,value.data(),value.size())
    );
    if(!reply){return false;}
    bool ok=(reply->type==REDIS_REPLY_STATUS&&std::string(reply->str)=="OK");
    freeReplyObject(reply);
    return ok;
}

bool CacheManager::redisGet(const std::string& key,std::string& value)
{
    if(!_redis){return false;}
    auto* reply=static_cast<redisReply*>(
        redisCommand(_redis, "GET %s", key.c_str())
    );
    if(!reply){return false;}
    bool ok=(reply->type==REDIS_REPLY_STRING);
    if(ok){value.assign(reply->str,reply->len);}
    freeReplyObject(reply);
    return ok;
}

//=====================================
// 关键字推荐
//=====================================
bool CacheManager::getKeyword(const std::string& query,std::string& result)
{
    // L1
    if(_kwCache.get(query,result)){return true;}

    // L2
    std::string l2key=kwKey(query);
    if(redisGet(l2key, result)){
        _kwCache.put(query,result); // 回写L1
        return true;
    }
    return false;
}

void CacheManager::putKeyword(const std::string& query,const std::string& result)
{
    _kwCache.put(query,result);
    redisSet(kwKey(query),result, KW_TTL); //L2写失败静默忽略
}

//=====================================
// 网页搜索
//=====================================
bool CacheManager::getSearch(const std::string& query,std::string& result)
{
    if(_searchCache.get(query,result)){return true;}
    std::string l2key=searchKey(query);
    if(redisGet(l2key, result)){
        _searchCache.put(query,result);
        return true;
    }
    return false;
}

void CacheManager::putSearch(const std::string& query,const std::string& result)
{
    _searchCache.put(query,result);
    redisSet(searchKey(query),result,SEARCH_TTL);
}
