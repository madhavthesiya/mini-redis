#include "InMemoryStorage.h"
#include <fstream>
#include <iostream>

InMemoryStorage::InMemoryStorage(size_t capacity){
    capacity_=capacity;
}
Result InMemoryStorage::exists(const std::string& key){
    Result res;
    auto it=data_.find(key);
    if(it==data_.end())
    {
        res.success=false;
        res.error=ErrorCode::KEY_NOT_FOUND;
        return res;
    }
    if(isExpired(it->second))
    {
        removeKey(key);
        res.success=false;
        res.error=ErrorCode::KEY_NOT_FOUND;
        return res;
    }
    res.success=true;
    res.error=ErrorCode::NONE;
    return res;
}

Result InMemoryStorage::get(const std::string& key){
    Result res;
    auto it=data_.find(key);
    if(it==data_.end())
    {
        res.success=false;
        res.error=ErrorCode::KEY_NOT_FOUND;
        return res;
    }
    if(isExpired(it->second))
    {
        removeKey(key);
        res.success=false;
        res.error=ErrorCode::KEY_NOT_FOUND;
        return res;
    }
    res.success=true;
    res.value=it->second.value;
    res.error=ErrorCode::NONE;
    touchLRU(key);
    return res;
}

Result InMemoryStorage::set(const std::string& key, const std::string& value){
    Result res;
    auto it=data_.find(key);
    if(it!=data_.end())
    {
        if(isExpired(it->second))
        {
            removeKey(key);
        }
        else
        {
            it->second.value=value;
            it->second.has_expiry=false;
            touchLRU(key);
            res.success=true;
            res.error=ErrorCode::NONE;
            return res;
        }
    }
    while(!lru_list_.empty())
    {
        auto backKey=lru_list_.back();
        auto it=data_.find(backKey);
        if(it!=data_.end() && isExpired(it->second))
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
    entry.has_expiry=false;
    // entry.has_expiry = true;     // for TTL
    // entry.expiry_time = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    entry.lru_it=lru_list_.begin();
    data_[key]=entry;
    res.success=true;
    res.error=ErrorCode::NONE;
    return res;   
}

Result InMemoryStorage::del(const std::string& key){
    Result res;
    auto it=data_.find(key);
    if(it!=data_.end())
    {
        lru_list_.erase(it->second.lru_it);
        data_.erase(it);
        res.success=true;
        res.error=ErrorCode::NONE;
    }
    else
    {
        res.success=false;
        res.error=ErrorCode::KEY_NOT_FOUND;
    }
    return res;
}

Result InMemoryStorage::saveToFile(const std::string& filename){
    Result res;
    std::ofstream out(filename);
    if(!out.is_open())
    {
        res.success=false;
        res.error=ErrorCode::INVALID_KEY;
        return res;
    }
    for(const auto& pair:data_)
    {
        out<<pair.first<<"="<<pair.second.value<<"\n";
    }
    out.close();
    res.success=true;
    res.error=ErrorCode::NONE;
    return res;
}

Result InMemoryStorage::loadFromFile(const std::string& filename){
    Result res;
    std::ifstream in(filename);
    if(!in.is_open())
    {
        res.success=false;
        res.error=ErrorCode::INVALID_KEY;
        return res;
    }
    data_.clear();     // clear our map 
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
    res.success=true;
    res.error=ErrorCode::NONE;
    return res;
}
    
void InMemoryStorage::touchLRU(const std::string& key){
    auto& entry=data_[key]; 
    lru_list_.erase(entry.lru_it); // delete current 
    lru_list_.push_front(key); // add to front 
    entry.lru_it=lru_list_.begin();
}

void InMemoryStorage::evictLRU(){
    const std::string& key=lru_list_.back();
    lru_list_.pop_back();     // delete last eleemnt from dll
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