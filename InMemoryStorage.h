#pragma once
#include "StorageInterface.h"
#include <unordered_map>
#include <string>
#include <chrono>
#include <list>
#include <shared_mutex>

class InMemoryStorage : public StorageInterface {
    public:
        explicit InMemoryStorage(size_t capacity=100);
        Result set(const std::string& key, const std::string& value) override;
        Result set(const std::string& key, const std::string& value, int ttlSeconds) override;
        Result get(const std::string& key) override;
        Result del(const std::string& key) override;
        Result exists(const std::string& key) override;
        Result expire(const std::string& key, int ttlSeconds) override;
        Result ttl(const std::string& key) override;
        Result saveToFile(const std::string& filename) override;
        Result loadFromFile(const std::string& filename) override;
    
    private:
        struct CacheEntry {
            std::string value;
            bool has_expiry;
            std::chrono::steady_clock::time_point expiry_time;
            std::list<std::string>::iterator lru_it;
        };
        void touchLRU(const std::string& key);
        void evictLRU();
        bool isExpired(const CacheEntry& entry) const;
        void removeKey(const std::string& key);
        Result setInternal(const std::string& key, const std::string& value, int ttlSeconds);
        std::unordered_map<std::string, CacheEntry> data_;
        std::list<std::string> lru_list_;
        size_t capacity_;
        mutable std::shared_mutex mutex_;   // reader-writer lock for thread safety
};