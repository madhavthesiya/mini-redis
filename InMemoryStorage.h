#pragma once
#include "StorageInterface.h"
#include <unordered_map>
#include <string>
#include <chrono>
#include <list>

class InMemoryStorage : public StorageInterface {
    public:
        explicit InMemoryStorage(size_t capacity=100);    // LRU capacity
        Result set(const std::string& key, const std::string& value) override;
        Result get(const std::string& key) override;
        Result del(const std::string& key) override;
        Result exists(const std::string& key) override;
        Result saveToFile(const std::string& filename) override;
        Result loadFromFile(const std::string& filename) override;
    
    private:
        struct CacheEntry {
            std::string value;
            bool has_expiry;
            std::chrono::steady_clock::time_point expiry_time;     // TTL
            std::list<std::string>::iterator lru_it;               // LRU
        };
        void touchLRU(const std::string& key);
        void evictLRU();
        bool isExpired(const CacheEntry& entry) const;
        void removeKey(const std::string& key);
        std::unordered_map<std::string, CacheEntry> data_;
        std::list<std::string> lru_list_;
        size_t capacity_;
};