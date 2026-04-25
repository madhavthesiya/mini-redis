#include "InMemoryStorage.h"
#include <fstream>
#include <iostream>
#include <mutex>

InMemoryStorage::InMemoryStorage(size_t capacity){
    capacity_=capacity;
}

// Uses unique_lock because lazy expiration may remove the key (write operation)
Result InMemoryStorage::exists(const std::string& key){
    std::unique_lock lock(mutex_);
    auto it=data_.find(key);
    if(it==data_.end())
    {
        return Result::fail(ErrorCode::KEY_NOT_FOUND);
    }
    if(isExpired(it->second))
    {
        removeKey(key);
        return Result::fail(ErrorCode::KEY_NOT_FOUND);
    }
    return Result::ok();
}

// Uses unique_lock because lazy expiration may remove the key + touchLRU modifies list
Result InMemoryStorage::get(const std::string& key){
    std::unique_lock lock(mutex_);
    auto it=data_.find(key);
    if(it==data_.end())
    {
        return Result::fail(ErrorCode::KEY_NOT_FOUND);
    }
    if(isExpired(it->second))
    {
        removeKey(key);
        return Result::fail(ErrorCode::KEY_NOT_FOUND);
    }
    touchLRU(key);
    return Result::ok(it->second.value);
}

Result InMemoryStorage::set(const std::string& key, const std::string& value){
    std::unique_lock lock(mutex_);
    return setInternal(key, value, 0);
}

Result InMemoryStorage::set(const std::string& key, const std::string& value, int ttlSeconds){
    std::unique_lock lock(mutex_);
    return setInternal(key, value, ttlSeconds);
}

// Private helper — caller must hold unique_lock
Result InMemoryStorage::setInternal(const std::string& key, const std::string& value, int ttlSeconds){
    auto it=data_.find(key);
    if(it!=data_.end())
    {
        if(isExpired(it->second))
        {
            removeKey(key);
        }
        else
        {
            // Update existing key
            it->second.value=value;
            if(ttlSeconds > 0)
            {
                it->second.has_expiry=true;
                it->second.expiry_time=std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);
            }
            else
            {
                it->second.has_expiry=false;
            }
            touchLRU(key);
            return Result::ok();
        }
    }
    // Sweep expired keys from the back before checking capacity
    while(!lru_list_.empty())
    {
        auto backKey=lru_list_.back();
        auto bit=data_.find(backKey);
        if(bit!=data_.end() && isExpired(bit->second))
        {
            removeKey(backKey);
        }
        else
        {
            break;
        }
    }
    if(data_.size()==capacity_)
    {
        evictLRU();
    }
    lru_list_.push_front(key);
    CacheEntry entry;
    entry.value=value;
    if(ttlSeconds > 0)
    {
        entry.has_expiry=true;
        entry.expiry_time=std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);
    }
    else
    {
        entry.has_expiry=false;
    }
    entry.lru_it=lru_list_.begin();
    data_[key]=entry;
    return Result::ok();
}

Result InMemoryStorage::del(const std::string& key){
    std::unique_lock lock(mutex_);
    auto it=data_.find(key);
    if(it!=data_.end())
    {
        lru_list_.erase(it->second.lru_it);
        data_.erase(it);
        return Result::ok();
    }
    return Result::fail(ErrorCode::KEY_NOT_FOUND);
}

Result InMemoryStorage::expire(const std::string& key, int ttlSeconds){
    std::unique_lock lock(mutex_);
    auto it=data_.find(key);
    if(it==data_.end())
    {
        return Result::fail(ErrorCode::KEY_NOT_FOUND);
    }
    if(isExpired(it->second))
    {
        removeKey(key);
        return Result::fail(ErrorCode::KEY_NOT_FOUND);
    }
    it->second.has_expiry=true;
    it->second.expiry_time=std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);
    return Result::ok();
}

// Pure read — uses shared_lock (multiple threads can read TTL concurrently)
Result InMemoryStorage::ttl(const std::string& key){
    std::shared_lock lock(mutex_);
    auto it=data_.find(key);
    if(it==data_.end())
    {
        return Result::fail(ErrorCode::KEY_NOT_FOUND);
    }
    if(isExpired(it->second))
    {
        // Don't remove here under shared_lock — just report not found
        return Result::fail(ErrorCode::KEY_NOT_FOUND);
    }
    if(!it->second.has_expiry)
    {
        return Result::ok("-1");
    }
    auto remaining=std::chrono::duration_cast<std::chrono::seconds>(
        it->second.expiry_time - std::chrono::steady_clock::now()
    ).count();
    return Result::ok(std::to_string(remaining));
}

// Snapshot read — uses shared_lock (doesn't modify in-memory state)
Result InMemoryStorage::saveToFile(const std::string& filename){
    std::shared_lock lock(mutex_);
    std::ofstream out(filename);
    if(!out.is_open())
    {
        return Result::fail(ErrorCode::FILE_ERROR);
    }
    for(const auto& pair:data_)
    {
        if(isExpired(pair.second)) continue;
        out<<pair.first<<"="<<pair.second.value<<"\n";
    }
    out.close();
    return Result::ok();
}

// Full rebuild — uses unique_lock (clears and replaces all data)
Result InMemoryStorage::loadFromFile(const std::string& filename){
    std::unique_lock lock(mutex_);
    std::ifstream in(filename);
    if(!in.is_open())
    {
        return Result::fail(ErrorCode::FILE_ERROR);
    }
    data_.clear();
    lru_list_.clear();
    
    std::string line;
    while(std::getline(in,line))
    {
        auto pos=line.find('=');
        if(pos!=std::string::npos)
        {
            std::string key=line.substr(0,pos);
            std::string value=line.substr(pos+1);
            lru_list_.push_back(key);
            CacheEntry entry;
            entry.value=value;
            entry.has_expiry=false;
            entry.lru_it=std::prev(lru_list_.end());
            data_[key]=entry;
        }
    }
    in.close();

    while(data_.size() > capacity_)
    {
        evictLRU();
    }

    return Result::ok();
}

// Private helpers — caller must already hold appropriate lock

void InMemoryStorage::touchLRU(const std::string& key){
    auto& entry=data_[key]; 
    lru_list_.erase(entry.lru_it);
    lru_list_.push_front(key);
    entry.lru_it=lru_list_.begin();
}

void InMemoryStorage::evictLRU(){
    std::string key=lru_list_.back();
    lru_list_.pop_back();
    data_.erase(key);
}

bool InMemoryStorage::isExpired(const CacheEntry& entry) const{
    if(!entry.has_expiry)
    {
        return false;   
    }
    return std::chrono::steady_clock::now()>=entry.expiry_time;
}

void InMemoryStorage::removeKey(const std::string& key){
    auto it=data_.find(key);
    if(it!=data_.end())
    {
        lru_list_.erase(it->second.lru_it);
        data_.erase(it);
    }
}