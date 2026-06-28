#pragma once

#include <cstddef>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <utility>
#include <deque>
#include <memory>
#include <vector>

// ============================================================
//  LruCache<K,V> — 单分片 LRU 纯数据结构
// ============================================================
// 数据结构: list<pair<K,V>>（头=最新,尾=最旧）+ unordered_map（O(1)查）

template<typename K,typename V>
class LRUCache
{
public:
    explicit LRUCache(size_t capacity):_capacity(capacity) {}

    bool get(const K& key,V& value){
        auto it=_map.find(key);
        if(it==_map.end()){return false;}

        _list.splice(_list.begin(),_list,it->second); // O(1)移到头部
        value=it->second->second;
        return true;
    }

    void put(const K& key,const V& value){
        auto it=_map.find(key);
        if(it!=_map.end()){
            it->second->second=value;
            _list.splice(_list.begin(),_list,it->second);
            return;
        }

        if(_list.size()>=_capacity){
            const auto& back=_list.back();
            _map.erase(back.first);
            _list.pop_back();
        }

        _list.emplace_front(key,value);
        _map[key]=_list.begin();
    }

private:
    size_t _capacity;
    std::list<std::pair<K,V>> _list;
    std::unordered_map<K, typename std::list<std::pair<K,V>>::iterator> _map;
};

// ============================================================
//  ShardedLruCache<K,V> — 分片 LRU，每片独立加锁
// ============================================================
// hash(key) % N → N 个 Shard，每个 Shard = LRUCache + shared_mutex
// 用 unique_ptr 管理 Shard，vector 只持有可移动的指针
template<typename K,typename V>
class ShardedLRUCache
{
public:
    explicit ShardedLRUCache(size_t totalCapacity, size_t shardCount=0)
    {
        if(shardCount==0){
            shardCount=std::thread::hardware_concurrency();
        }
        if(shardCount==0){shardCount=4;}
        size_t perShard=std::max(size_t(1),totalCapacity/shardCount);
        _shards.reserve(shardCount);
        for(size_t i=0;i<shardCount;++i){
            _shards.push_back(std::make_unique<Shard>(perShard));
        }
    }

    bool get(const K& key,V& value){
        size_t idx=std::hash<K>{}(key)%_shards.size();
        auto& s=*_shards[idx];
        std::shared_lock lock(s.mutex);
        return s.cache.get(key,value);
    }
    void put(const K& key,const V& value){
        size_t idx=std::hash<K>{}(key)%_shards.size();
        auto& s=*_shards[idx];
        std::unique_lock lock(s.mutex);
        s.cache.put(key,value);
    }

private:
    struct Shard{
        LRUCache<K,V> cache;
        mutable std::shared_mutex mutex;
        Shard(size_t cap):cache(cap){}
    };
    std::vector<std::unique_ptr<Shard>> _shards;
};
